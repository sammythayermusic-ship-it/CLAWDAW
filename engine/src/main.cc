#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <google/protobuf/empty.pb.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <tracktion_engine/tracktion_engine.h>

#include "daw/v1/engine.grpc.pb.h"
#include "daw/v1/project.pb.h"
#include "daw/v1/common.pb.h"

namespace te = tracktion::engine;
namespace tc = tracktion;  // TimePosition / TimeDuration live here, not in te::

namespace {

// ----------------------------------------------------------------------------
// Conversion helpers between Tracktion types and our proto messages.
// ----------------------------------------------------------------------------

std::string makeCommitId() {
    return juce::Uuid().toString().toStdString();
}

void setProtoTime(daw::v1::TimePosition* dst, tc::TimePosition pos) {
    if (dst != nullptr) dst->set_seconds(pos.inSeconds());
}

void setProtoDuration(daw::v1::TimePosition* dst, tc::TimeDuration dur) {
    if (dst != nullptr) dst->set_seconds(dur.inSeconds());
}

uint32_t protoColorRgba(juce::Colour c) {
    return ((uint32_t) c.getRed()   << 24)
         | ((uint32_t) c.getGreen() << 16)
         | ((uint32_t) c.getBlue()  <<  8)
         | ((uint32_t) c.getAlpha());
}

// MIDI tracks in Tracktion are AudioTracks that hold MIDI clips — class alone
// can't tell us which is which. For TrackSummary we report AUDIO for both;
// callers that care can use GetTrack and inspect the regions.
daw::v1::TrackType protoTrackType(const te::Track& track) {
    if (track.isMasterTrack()) return daw::v1::TRACK_TYPE_BUS;
    if (track.isFolderTrack()) return daw::v1::TRACK_TYPE_FOLDER;
    if (track.isAudioTrack())  return daw::v1::TRACK_TYPE_AUDIO;
    return daw::v1::TRACK_TYPE_UNSPECIFIED;
}

void fillTrackSummary(daw::v1::TrackSummary* dst, te::Track& track) {
    dst->set_id(track.itemID.toString().toStdString());
    dst->set_name(track.getName().toStdString());
    dst->set_type(protoTrackType(track));
    dst->mutable_color()->set_rgba(protoColorRgba(track.getColour()));
    dst->set_muted(track.isMuted(false));
    dst->set_soloed(track.isSolo(false));
    dst->set_plugin_count((uint32_t) track.getAllPlugins().size());
    if (auto* clipOwner = dynamic_cast<te::ClipOwner*>(&track)) {
        dst->set_region_count((uint32_t) clipOwner->getClips().size());
    }
}

te::Track* findTrackById(te::Edit& edit, const std::string& id) {
    auto eid = te::EditItemID::fromString(juce::String(id));
    if (! eid.isValid()) return nullptr;
    return te::findTrackForID(edit, eid);
}

// Acquire the JUCE message-thread lock for the duration of `fn`. We need this
// for any RPC that mutates the Edit's value tree (rename, volume, pan, etc.):
// JUCE asserts that ValueTree mutations happen on the message thread or with
// the lock held. The constructor blocks until the message thread yields the
// lock, so main() must be running the JUCE dispatch loop concurrently.
//
// The lock is re-entrant — calling this from the message thread itself is
// cheap, so we don't bother special-casing that path.
template <typename Fn>
auto runOnMessageThread(Fn&& fn) -> decltype(fn()) {
    const juce::MessageManagerLock mml;
    return fn();
}

void fillMutationResult(daw::v1::MutationResult* dst, std::string description) {
    dst->set_commit_id(makeCommitId());
    dst->set_description(std::move(description));
}

// ----------------------------------------------------------------------------
// gRPC service implementation.
// ----------------------------------------------------------------------------

class EngineServiceImpl final : public daw::v1::Engine::Service {
public:
    EngineServiceImpl(te::Engine& engine, te::Edit& edit) : engine_(engine), edit_(edit) {}

    // -------- Reads --------

    grpc::Status ListTracks(grpc::ServerContext* /*context*/,
                            const google::protobuf::Empty* /*request*/,
                            daw::v1::ListTracksResponse* response) override {
        for (auto* track : te::getAllTracks(edit_)) {
            fillTrackSummary(response->add_tracks(), *track);
        }
        return grpc::Status::OK;
    }

    grpc::Status GetProject(grpc::ServerContext* /*context*/,
                            const google::protobuf::Empty* /*request*/,
                            daw::v1::Project* response) override {
        response->set_name(edit_.getName().toStdString());
        response->set_tempo_bpm(edit_.tempoSequence.getBpmAt(tc::TimePosition::fromSeconds(0)));
        response->set_sample_rate((uint32_t) engine_.getDeviceManager().getSampleRate());
        setProtoDuration(response->mutable_length(), edit_.getLength());

        const auto& timeSigs = edit_.tempoSequence.getTimeSigs();
        if (! timeSigs.isEmpty() && timeSigs.getFirst() != nullptr) {
            auto* ts = timeSigs.getFirst();
            response->mutable_time_signature()->set_numerator((uint32_t) ts->numerator.get());
            response->mutable_time_signature()->set_denominator((uint32_t) ts->denominator.get());
        }

        // Key: Tracktion stores a chord sequence rather than a single project
        // key. Leaving Key empty for v1 — a richer mapping can come later.

        for (auto* track : te::getAllTracks(edit_)) {
            fillTrackSummary(response->add_tracks(), *track);
        }

        for (auto* m : edit_.getMarkerManager().getMarkers()) {
            if (m == nullptr) continue;
            auto* dst = response->add_markers();
            dst->set_name(m->getName().toStdString());
            setProtoTime(dst->mutable_position(), m->getPosition().getStart());
            dst->mutable_color()->set_rgba(protoColorRgba(m->getColour()));
        }

        // project_path: empty for an in-memory edit.
        response->set_schema_version(1);
        return grpc::Status::OK;
    }

    grpc::Status GetTrack(grpc::ServerContext* /*context*/,
                          const daw::v1::GetTrackRequest* request,
                          daw::v1::Track* response) override {
        auto* track = findTrackById(edit_, request->track_id());
        if (track == nullptr) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Track not found: " + request->track_id());
        }

        response->set_id(track->itemID.toString().toStdString());
        response->set_name(track->getName().toStdString());
        response->set_type(protoTrackType(*track));
        response->mutable_color()->set_rgba(protoColorRgba(track->getColour()));

        auto* mixer = response->mutable_mixer();
        mixer->set_muted(track->isMuted(false));
        mixer->set_soloed(track->isSolo(false));
        if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(track)) {
            if (auto* vp = audioTrack->getVolumePlugin()) {
                mixer->set_volume_db(vp->getVolumeDb());
                mixer->set_pan(vp->getPan());
            }
        }

        for (auto* plugin : track->getAllPlugins()) {
            if (plugin == nullptr) continue;
            auto* pi = response->add_plugins();
            pi->set_id(plugin->itemID.toString().toStdString());
            pi->set_track_id(track->itemID.toString().toStdString());
            pi->set_bypassed(! plugin->isEnabled());

            auto* info = pi->mutable_info();
            info->set_id(plugin->getIdentifierString().toStdString());
            info->set_name(plugin->getName().toStdString());
            info->set_vendor(plugin->getVendor().toStdString());
            // format/category and the rest of PluginInfo: filled when we wire
            // the scanner; the internal volume/pan/mute plugins don't carry
            // that metadata in a useful form anyway.
        }

        if (auto* clipOwner = dynamic_cast<te::ClipOwner*>(track)) {
            for (auto* clip : clipOwner->getClips()) {
                if (clip == nullptr) continue;
                const auto pos = clip->getPosition();
                if (clip->isMidi()) {
                    auto* mr = response->add_midi_regions();
                    mr->set_id(clip->itemID.toString().toStdString());
                    mr->set_track_id(track->itemID.toString().toStdString());
                    mr->set_name(clip->getName().toStdString());
                    setProtoTime(mr->mutable_start(), pos.getStart());
                    setProtoDuration(mr->mutable_length(), pos.getLength());
                    mr->set_muted(clip->isMuted());
                    // Note iteration deferred — needs MidiList traversal.
                } else if (auto* audioClip = dynamic_cast<te::AudioClipBase*>(clip)) {
                    auto* ar = response->add_audio_regions();
                    ar->set_id(audioClip->itemID.toString().toStdString());
                    ar->set_track_id(track->itemID.toString().toStdString());
                    ar->set_name(audioClip->getName().toStdString());
                    setProtoTime(ar->mutable_start(), pos.getStart());
                    setProtoDuration(ar->mutable_length(), pos.getLength());
                    setProtoDuration(ar->mutable_source_offset(), pos.getOffset());
                    ar->set_audio_file_path(audioClip->getCurrentSourceFile().getFullPathName().toStdString());
                    ar->set_gain_db(audioClip->getGainDB());
                    ar->set_fade_in_seconds(audioClip->getFadeIn().inSeconds());
                    ar->set_fade_out_seconds(audioClip->getFadeOut().inSeconds());
                    ar->set_reversed(audioClip->getIsReversed());
                    ar->set_muted(audioClip->isMuted());
                }
                // Other clip types (markers, chord, tempo) aren't tracks the
                // user typically interacts with — skip silently.
            }
        }

        return grpc::Status::OK;
    }

    // -------- Mutations --------

    grpc::Status RenameTrack(grpc::ServerContext* /*context*/,
                             const daw::v1::RenameTrackRequest* request,
                             daw::v1::MutationResult* response) override {
        auto* track = findTrackById(edit_, request->track_id());
        if (track == nullptr) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Track not found: " + request->track_id());
        }

        runOnMessageThread([&] {
            edit_.getUndoManager().beginNewTransaction("Rename track");
            track->setName(juce::String(request->name()));
        });

        fillMutationResult(response, "Renamed track to \"" + request->name() + "\"");
        return grpc::Status::OK;
    }

    grpc::Status SetTrackVolume(grpc::ServerContext* /*context*/,
                                const daw::v1::SetTrackVolumeRequest* request,
                                daw::v1::MutationResult* response) override {
        auto* track = findTrackById(edit_, request->track_id());
        if (track == nullptr) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Track not found: " + request->track_id());
        }
        auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
        if (audioTrack == nullptr) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "Track has no volume plugin (not an audio track)");
        }
        auto* vp = audioTrack->getVolumePlugin();
        if (vp == nullptr) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "Track has no volume plugin");
        }

        // KNOWN LIMITATION: volume changes are NOT undoable in v1.
        //
        // Tracktion's setVolumeDb routes through an AutomatableParameter,
        // which intentionally bypasses the UndoManager (parameter changes
        // happen at audio rate, so undoing every parameter touch would
        // explode the undo stack). We tried writing directly to the
        // underlying ValueTree property with the UndoManager attached, but
        // AutomatableParameter::valueTreePropertyChanged explicitly does
        // NOT propagate ValueTree changes to the parameter's currentValue —
        // the comment in tracktion_AutomatableParameter.cpp:883-884 is
        // explicit: "You shouldn't be directly setting the value of an
        // attachedValue managed parameter".
        //
        // The proper fix is a side-band undo bridge for parameter changes
        // (record before/after pairs in our own commit_id map). Tracked as
        // a Phase 2 follow-up.
        const float dbValue = (float) request->volume_db();
        runOnMessageThread([&] {
            edit_.getUndoManager().beginNewTransaction("Set track volume");
            vp->setVolumeDb(dbValue);
        });

        fillMutationResult(response, "Set track volume to " + std::to_string(dbValue) + " dB");
        return grpc::Status::OK;
    }

    grpc::Status SetTrackPan(grpc::ServerContext* /*context*/,
                             const daw::v1::SetTrackPanRequest* request,
                             daw::v1::MutationResult* response) override {
        auto* track = findTrackById(edit_, request->track_id());
        if (track == nullptr) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Track not found: " + request->track_id());
        }
        auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
        if (audioTrack == nullptr) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "Track has no pan plugin (not an audio track)");
        }
        auto* vp = audioTrack->getVolumePlugin();
        if (vp == nullptr) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "Track has no pan plugin");
        }

        const float panValue = (float) request->pan();
        if (panValue < -1.0f || panValue > 1.0f) {
            return grpc::Status(grpc::StatusCode::OUT_OF_RANGE,
                                "Pan must be in [-1.0, 1.0]");
        }

        // Same KNOWN LIMITATION as SetTrackVolume — pan changes are not
        // undoable in v1. See the comment there for the gory detail.
        runOnMessageThread([&] {
            edit_.getUndoManager().beginNewTransaction("Set track pan");
            vp->setPan(panValue);
        });

        fillMutationResult(response, "Set track pan to " + std::to_string(panValue));
        return grpc::Status::OK;
    }

    grpc::Status Undo(grpc::ServerContext* /*context*/,
                      const daw::v1::UndoRequest* /*request*/,
                      daw::v1::MutationResult* response) override {
        // commit_id-targeted undo is in the proto contract, but Tracktion's
        // UndoManager only undoes the most recent transaction in order. For
        // v1 we ignore commit_id and undo the last transaction; if the agent
        // wants finer control we'll need to layer our own bookkeeping.
        bool ok = false;
        runOnMessageThread([&] {
            ok = edit_.getUndoManager().undo();
        });

        if (! ok) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "Nothing to undo");
        }

        fillMutationResult(response, "Undo");
        return grpc::Status::OK;
    }

private:
    te::Engine& engine_;
    te::Edit& edit_;
};

}  // namespace

// ----------------------------------------------------------------------------
// SIGINT/SIGTERM → ask the JUCE dispatch loop to stop so main() can return
// cleanly and shut down the gRPC server. Without this, a Ctrl+C just kills
// the process and gRPC clients see an abrupt disconnect.
// ----------------------------------------------------------------------------

namespace {
std::atomic<bool> g_shutdown_requested{false};

void signalHandler(int /*signal*/) {
    g_shutdown_requested.store(true);
    // stopDispatchLoop is a no-op for runDispatchLoopUntil, but harmless and
    // sets JUCE's internal `quitMessagePosted` flag in case anything checks it.
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating()) {
        mm->stopDispatchLoop();
    }
}
}  // namespace

int main(int argc, char** argv) {
    const std::string address = (argc > 1) ? argv[1] : "127.0.0.1:50051";

    // Initialise JUCE on the main thread; this thread becomes the message
    // thread for the rest of the process.
    juce::ScopedJuceInitialiser_GUI juce_init;

    // Tracktion's Engine constructor wires up plugin scanning, device
    // management, and the engine-wide settings store. One per process.
    te::Engine engine{"CLAWDAW"};

    auto edit = te::Edit::createSingleTrackEdit(engine);
    edit->ensureNumberOfAudioTracks(2);

    EngineServiceImpl service{engine, *edit};

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    if (! server) {
        std::cerr << "Failed to start gRPC server on " << address << "\n";
        return 1;
    }

    std::cout << "clawdaw_engine listening on " << address
              << " (tracks in edit: " << te::getAllTracks(*edit).size() << ")\n";

    // gRPC's blocking Wait() lives on a worker thread. The main thread pumps
    // the JUCE message queue so that RPC handlers calling MessageManagerLock
    // from worker threads can actually obtain the lock instead of deadlocking.
    // (runDispatchLoop() doesn't work in a headless console process on macOS;
    // it relies on a platform run loop we never start. The TestRunner example
    // uses this same runDispatchLoopUntil idiom.)
    std::thread grpc_thread([&] { server->Wait(); });

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    while (! g_shutdown_requested.load()) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    server->Shutdown();
    grpc_thread.join();
    return 0;
}
