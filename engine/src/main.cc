#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <google/protobuf/empty.pb.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <tracktion_engine/tracktion_engine.h>

#include "daw/v1/engine.grpc.pb.h"
#include "daw/v1/project.pb.h"
#include "daw/v1/common.pb.h"
#include "daw/v1/plugin.pb.h"
#include "daw/v1/transport.pb.h"
#include "daw/v1/events.pb.h"

namespace te = tracktion::engine;
namespace tc = tracktion;  // TimePosition / TimeDuration live here, not in te::

// Set by the SIGINT/SIGTERM handler. Read by main()'s pump loop and by
// long-running RPC handlers (notably RescanPlugins) so they can break out
// rather than running to completion when the operator wants to exit.
namespace { std::atomic<bool> g_shutdown_requested{false}; }

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

// Like runOnMessageThread, but dispatches via callAsync + std::future instead
// of holding MessageManagerLock. AU plugin instantiation/destruction needs
// the message thread to be ACTIVELY dispatching (not paused), so anything
// that touches plugin lifecycle must use this variant. Plain ValueTree
// mutations should keep using runOnMessageThread — it's cheaper and works.
inline void runOnMessageThreadSync(std::function<void()> fn) {
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        fn();
        return;
    }
    std::promise<void> done;
    auto fut = done.get_future();
    juce::MessageManager::callAsync([fn = std::move(fn), &done]() mutable {
        fn();
        done.set_value();
    });
    fut.wait();
}

void fillMutationResult(daw::v1::MutationResult* dst,
                        const std::string& commit_id,
                        std::string description) {
    dst->set_commit_id(commit_id);
    dst->set_description(std::move(description));
}

// ----------------------------------------------------------------------------
// Plugin helpers.
// ----------------------------------------------------------------------------

daw::v1::PluginFormat protoPluginFormat(const juce::String& fmt) {
    if (fmt == te::PluginManager::builtInPluginFormatName) return daw::v1::PLUGIN_FORMAT_INTERNAL;
    if (fmt == "VST3")      return daw::v1::PLUGIN_FORMAT_VST3;
    if (fmt == "AudioUnit") return daw::v1::PLUGIN_FORMAT_AU;
    if (fmt == "CLAP")      return daw::v1::PLUGIN_FORMAT_CLAP;
    return daw::v1::PLUGIN_FORMAT_UNSPECIFIED;
}

// JUCE PluginDescription.category is free-text. We do a heuristic substring
// match against the proto enum. Order matters: more specific keywords come
// first so e.g. "Compressor" doesn't fall through to the generic "Effect"
// bucket. The instrument fallback intentionally runs last so a plugin that
// describes itself as "Synth Reverb" still ends up under REVERB.
daw::v1::PluginCategory protoPluginCategory(const juce::String& cat, bool isInstrument) {
    const auto c = cat.toLowerCase();
    if (c.contains("eq"))                                          return daw::v1::PLUGIN_CATEGORY_EQ;
    if (c.contains("comp") || c.contains("limit") || c.contains("dynamic")) return daw::v1::PLUGIN_CATEGORY_DYNAMICS;
    if (c.contains("reverb"))                                      return daw::v1::PLUGIN_CATEGORY_REVERB;
    if (c.contains("delay") || c.contains("echo"))                 return daw::v1::PLUGIN_CATEGORY_DELAY;
    if (c.contains("chorus") || c.contains("phaser") || c.contains("flanger") || c.contains("modulat")) return daw::v1::PLUGIN_CATEGORY_MODULATION;
    if (c.contains("distort") || c.contains("saturat") || c.contains("overdrive")) return daw::v1::PLUGIN_CATEGORY_DISTORTION;
    if (c.contains("analy") || c.contains("meter"))                return daw::v1::PLUGIN_CATEGORY_ANALYZER;
    if (c.contains("util"))                                        return daw::v1::PLUGIN_CATEGORY_UTILITY;
    if (isInstrument || c.contains("synth") || c.contains("instrument") || c.contains("sampler")) return daw::v1::PLUGIN_CATEGORY_INSTRUMENT;
    return daw::v1::PLUGIN_CATEGORY_EFFECT;
}

void fillPluginInfo(daw::v1::PluginInfo* dst, const juce::PluginDescription& d) {
    dst->set_id(d.createIdentifierString().toStdString());
    dst->set_name(d.name.toStdString());
    dst->set_vendor(d.manufacturerName.toStdString());
    dst->set_format(protoPluginFormat(d.pluginFormatName));
    dst->set_category(protoPluginCategory(d.category, d.isInstrument));
    dst->set_is_instrument(d.isInstrument);
    dst->set_version(d.version.toStdString());
    dst->set_file_path(d.fileOrIdentifier.toStdString());
}

void fillPluginParameter(daw::v1::PluginParameter* dst, te::AutomatableParameter& p) {
    const auto range = p.getValueRange();
    dst->set_id(p.paramID.toStdString());
    dst->set_name(p.getParameterName().toStdString());
    dst->set_value(p.getCurrentValue());
    dst->set_normalized(p.getCurrentNormalisedValue());
    dst->set_min(range.getStart());
    dst->set_max(range.getEnd());
    dst->set_unit(p.getLabel().toStdString());
    dst->set_automatable(true);  // every AutomatableParameter is, by definition.
    dst->set_display_value(p.valueToString(p.getCurrentValue()).toStdString());
}

te::Plugin::Ptr findPluginById(te::Edit& edit, const std::string& id) {
    auto eid = te::EditItemID::fromString(juce::String(id));
    if (! eid.isValid()) return {};
    return te::findPluginForID(edit, eid);
}

// ----------------------------------------------------------------------------
// Event broadcaster: backs the SubscribeEvents server-streaming RPC.
//
// Each call to SubscribeEvents creates a Subscription with its own bounded
// event queue + condition variable. Mutating handlers call broadcast(evt);
// the broadcaster drops a copy into every active subscriber's queue (after
// applying that subscriber's filter), and the SubscribeEvents handler thread
// drains the queue onto the wire.
//
// Lifetime: Subscription is owned by the SubscribeEvents handler (shared_ptr
// kept on its stack); the broadcaster only holds weak_ptrs. When the handler
// returns, the Subscription is destroyed and broadcasts skip the expired
// weak_ptrs. No explicit unsubscribe is needed.
// ----------------------------------------------------------------------------

const char* eventTypeName(const daw::v1::Event& e) {
    using P = daw::v1::Event;
    switch (e.payload_case()) {
        case P::kProjectLoaded:         return "project_loaded";
        case P::kProjectSaved:          return "project_saved";
        case P::kTempoChanged:          return "tempo_changed";
        case P::kKeyChanged:            return "key_changed";
        case P::kTrackAdded:            return "track_added";
        case P::kTrackRemoved:          return "track_removed";
        case P::kTrackRenamed:          return "track_renamed";
        case P::kTrackMixerChanged:     return "track_mixer_changed";
        case P::kRegionAdded:           return "region_added";
        case P::kRegionRemoved:         return "region_removed";
        case P::kRegionMoved:           return "region_moved";
        case P::kPluginAdded:           return "plugin_added";
        case P::kPluginRemoved:         return "plugin_removed";
        case P::kPluginParamChanged:    return "plugin_param_changed";
        case P::kPluginBypassed:        return "plugin_bypassed";
        case P::kRecordStarted:         return "record_started";
        case P::kRecordComplete:        return "record_complete";
        case P::kTransportStateChanged: return "transport_state_changed";
        case P::kCommandApplied:        return "command_applied";
        case P::kCommandUndone:         return "command_undone";
        case P::PAYLOAD_NOT_SET:        return "";
    }
    return "";
}

// Returns the track_id this event scopes to, or empty string if it doesn't
// scope to any single track. Events that don't scope to a track are emitted
// to subscribers regardless of the track_ids filter.
std::string eventTrackId(const daw::v1::Event& e) {
    using P = daw::v1::Event;
    switch (e.payload_case()) {
        case P::kTrackAdded:        return e.track_added().track_id();
        case P::kTrackRemoved:      return e.track_removed().track_id();
        case P::kTrackRenamed:      return e.track_renamed().track_id();
        case P::kTrackMixerChanged: return e.track_mixer_changed().track_id();
        case P::kRegionAdded:       return e.region_added().track_id();
        case P::kPluginAdded:       return e.plugin_added().track_id();
        case P::kRecordStarted:     return e.record_started().track_id();
        case P::kRecordComplete:    return e.record_complete().track_id();
        default:                    return "";
    }
}

bool matchesFilter(const daw::v1::Event& e, const daw::v1::EventFilter& f) {
    if (! f.event_types().empty()) {
        const std::string name = eventTypeName(e);
        bool found = false;
        for (const auto& t : f.event_types()) {
            if (t == name) { found = true; break; }
        }
        if (! found) return false;
    }
    if (! f.track_ids().empty()) {
        const std::string tid = eventTrackId(e);
        if (tid.empty()) return true;  // event has no track scope; let through
        bool found = false;
        for (const auto& t : f.track_ids()) {
            if (t == tid) { found = true; break; }
        }
        if (! found) return false;
    }
    return true;
}

class EventBroadcaster {
public:
    struct Subscription {
        daw::v1::EventFilter filter;
        std::deque<daw::v1::Event> queue;
        std::mutex queue_mutex;
        std::condition_variable cv;
        // If a slow consumer hits this size we drop the OLDEST event to make
        // room. Better than blocking the broadcaster or unbounded growth.
        static constexpr size_t kMaxQueueSize = 1024;
    };
    using Ptr = std::shared_ptr<Subscription>;

    Ptr subscribe(daw::v1::EventFilter filter) {
        auto sub = std::make_shared<Subscription>();
        sub->filter = std::move(filter);
        std::lock_guard<std::mutex> lock(subs_mutex_);
        subs_.push_back(sub);
        return sub;
    }

    void broadcast(const daw::v1::Event& evt) {
        std::vector<Ptr> live;
        {
            std::lock_guard<std::mutex> lock(subs_mutex_);
            // Compact expired weaks while we're here.
            for (auto it = subs_.begin(); it != subs_.end();) {
                if (auto p = it->lock()) { live.push_back(p); ++it; }
                else                      { it = subs_.erase(it); }
            }
        }
        for (auto& sub : live) {
            if (! matchesFilter(evt, sub->filter)) continue;
            {
                std::lock_guard<std::mutex> lock(sub->queue_mutex);
                if (sub->queue.size() >= Subscription::kMaxQueueSize) {
                    sub->queue.pop_front();
                }
                sub->queue.push_back(evt);
            }
            sub->cv.notify_one();
        }
    }

    // Wake every subscription so SubscribeEvents handlers can re-check
    // shutdown flags and exit cleanly.
    void wakeAll() {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        for (auto& w : subs_) {
            if (auto p = w.lock()) p->cv.notify_all();
        }
    }

private:
    std::mutex subs_mutex_;
    std::vector<std::weak_ptr<Subscription>> subs_;
};

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

    grpc::Status AddTrack(grpc::ServerContext* /*context*/,
                          const daw::v1::AddTrackRequest* request,
                          daw::v1::AddTrackResponse* response) override {
        // v1: only AUDIO. MIDI tracks in Tracktion are AudioTracks that hold
        // MIDI clips, so insertNewAudioTrack would technically work for them
        // too, but our TrackSummary type-reporter (protoTrackType) can't
        // distinguish them anyway — both come back as AUDIO. To keep the
        // tool surface honest, return INVALID_ARGUMENT for everything that
        // isn't AUDIO; we'll expand once the engine can faithfully round-trip
        // the others.
        if (request->type() != daw::v1::TRACK_TYPE_AUDIO) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "Only TRACK_TYPE_AUDIO is supported in v1");
        }

        const auto commit_id = makeCommitId();
        const std::string requestedName = request->name();

        std::string new_track_id;
        std::string actualName;
        // insertNewAudioTrack triggers TrackList's AsyncUpdater (and likely
        // creates the default volume/level-meter plugins under the hood),
        // both of which need the message thread to be *actively dispatching*
        // — same constraint as the AddPlugin handler. Holding
        // MessageManagerLock from a worker thread blocks the dispatch loop
        // and the call hangs forever. Use the callAsync+future pattern.
        runOnMessageThreadSync([&] {
            auto trackPtr = edit_.insertNewAudioTrack(
                te::TrackInsertPoint::getEndOfTracks(edit_),
                /*SelectionManager*/ nullptr,
                /*addDefaultPlugins*/ true);
            if (trackPtr == nullptr) return;
            if (! requestedName.empty()) {
                trackPtr->setName(juce::String(requestedName));
            }
            new_track_id = trackPtr->itemID.toString().toStdString();
            actualName = trackPtr->getName().toStdString();
        });

        if (new_track_id.empty()) {
            return grpc::Status(grpc::StatusCode::INTERNAL,
                                "insertNewAudioTrack returned null");
        }

        const std::string description = "Added audio track: " + actualName;

        {
            daw::v1::Event e;
            e.mutable_track_added()->set_track_id(new_track_id);
            emitEvent(std::move(e));
        }

        // Undo: re-find the track by id (the pointer would dangle if any
        // intervening op removed it) and call edit_.deleteTrack on it. Same
        // callAsync rationale as the insertion path. KNOWN LIMITATION: if a
        // sibling DeleteTrack+Undo cycle has happened on this track in the
        // interim, Tracktion's track cache may return a Track whose `state`
        // ValueTree is detached, and deleteTrack's `state.getParent().removeChild`
        // becomes a no-op. See the 2026-05-11 session-log entry under
        // "Known limitations." pytest's add+undo path passes; the MCP smoke
        // sidesteps the multi-mutation cleanup that triggers it.
        pushUndo(commit_id, description, [this, new_track_id] {
            runOnMessageThreadSync([this, new_track_id] {
                if (auto* track = findTrackById(edit_, new_track_id)) {
                    edit_.deleteTrack(track);
                }
            });
            daw::v1::Event e;
            e.mutable_track_removed()->set_track_id(new_track_id);
            emitEvent(std::move(e));
        });

        emitCommandApplied(commit_id, description);
        response->set_track_id(new_track_id);
        fillMutationResult(response->mutable_mutation(), commit_id, description);
        return grpc::Status::OK;
    }

    grpc::Status DeleteTrack(grpc::ServerContext* /*context*/,
                             const daw::v1::DeleteTrackRequest* request,
                             daw::v1::MutationResult* response) override {
        // Confirmation gate per the proto contract — every Delete*/Remove*
        // RPC requires confirm=true so an agent typo doesn't nuke a track.
        if (! request->confirm()) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "DeleteTrack requires confirm=true");
        }
        auto* track = findTrackById(edit_, request->track_id());
        if (track == nullptr) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Track not found: " + request->track_id());
        }
        // Master / global tracks (Tempo, Marker, Chord, Arranger) aren't
        // user-created — refuse to delete them.
        if (track->isMasterTrack() || ! te::TrackList::isMovableTrack(track->state)) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "Cannot delete master or global track");
        }

        const auto commit_id = makeCommitId();
        const std::string track_id = request->track_id();
        const std::string description = "Deleted track: " + track->getName().toStdString();

        // Capture the track's full ValueTree as XML before deletion so undo
        // can reconstruct it with plugins, regions, and mixer state intact.
        // Also remember the preceding top-level track so undo restores at
        // the same index. deleteTrack tears down plugins, which (same as
        // their construction) needs the dispatch loop running — use the
        // callAsync variant.
        std::string capturedXml;
        std::string precedingTrackId;
        runOnMessageThreadSync([&] {
            capturedXml = track->state.toXmlString().toStdString();

            te::Track* prev = nullptr;
            edit_.visitAllTopLevelTracks([&](te::Track& t) {
                if (&t == track) return false;  // stop; prev holds the predecessor
                prev = &t;
                return true;
            });
            if (prev != nullptr) {
                precedingTrackId = prev->itemID.toString().toStdString();
            }

            edit_.deleteTrack(track);
        });

        {
            daw::v1::Event e;
            e.mutable_track_removed()->set_track_id(track_id);
            emitEvent(std::move(e));
        }

        pushUndo(commit_id, description,
                 [this, track_id, capturedXml, precedingTrackId] {
            std::string restored_id;
            runOnMessageThreadSync([&] {
                auto vt = juce::ValueTree::fromXml(juce::String(capturedXml));
                if (! vt.isValid()) return;
                // Parent is invalid (top-level); preceding is the captured
                // sibling, or invalid to insert at the head of the list.
                te::EditItemID parentID;  // invalid → top level
                auto precedingID = precedingTrackId.empty()
                                       ? te::EditItemID()
                                       : te::EditItemID::fromString(juce::String(precedingTrackId));
                auto restored = edit_.insertTrack(
                    te::TrackInsertPoint(parentID, precedingID),
                    vt,
                    /*SelectionManager*/ nullptr);
                if (restored != nullptr) {
                    restored_id = restored->itemID.toString().toStdString();
                }
            });
            // The original ID was free (we just deleted it) so insertTrack
            // should preserve it, but if Tracktion remapped, prefer the
            // restored id in the event so subscribers track the live track.
            daw::v1::Event e;
            e.mutable_track_added()->set_track_id(
                restored_id.empty() ? track_id : restored_id);
            emitEvent(std::move(e));
        });

        emitCommandApplied(commit_id, description);
        fillMutationResult(response, commit_id, description);
        return grpc::Status::OK;
    }

    grpc::Status RenameTrack(grpc::ServerContext* /*context*/,
                             const daw::v1::RenameTrackRequest* request,
                             daw::v1::MutationResult* response) override {
        auto* track = findTrackById(edit_, request->track_id());
        if (track == nullptr) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Track not found: " + request->track_id());
        }

        const auto commit_id = makeCommitId();
        const std::string track_id = track->itemID.toString().toStdString();
        const std::string beforeName = track->getName().toStdString();
        const std::string newName = request->name();
        const std::string description = "Renamed track to \"" + newName + "\"";

        runOnMessageThread([&] { track->setName(juce::String(newName)); });

        // Type-specific event for the new state.
        {
            daw::v1::Event e;
            auto* tr = e.mutable_track_renamed();
            tr->set_track_id(track_id);
            tr->set_new_name(newName);
            emitEvent(std::move(e));
        }

        // Push the undo. The closure does the revert AND emits the inverse
        // type-specific event so streaming subscribers see the rename undone.
        pushUndo(commit_id, description, [this, track, track_id, beforeName] {
            runOnMessageThread([&] { track->setName(juce::String(beforeName)); });
            daw::v1::Event e;
            auto* tr = e.mutable_track_renamed();
            tr->set_track_id(track_id);
            tr->set_new_name(beforeName);
            emitEvent(std::move(e));
        });

        emitCommandApplied(commit_id, description);
        fillMutationResult(response, commit_id, description);
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

        // Capture before-value, run the setter, push a replay closure into
        // the side-band undo log. See the comment on undo_log_ for why we
        // don't go through Tracktion's UndoManager.
        const auto commit_id = makeCommitId();
        const std::string track_id = track->itemID.toString().toStdString();
        const float beforeDb = vp->getVolumeDb();
        const float dbValue = (float) request->volume_db();
        const std::string description = "Set track volume to " + std::to_string(dbValue) + " dB";

        runOnMessageThread([&] { vp->setVolumeDb(dbValue); });

        {
            daw::v1::Event e;
            e.mutable_track_mixer_changed()->set_track_id(track_id);
            emitEvent(std::move(e));
        }

        pushUndo(commit_id, description, [this, vp, track_id, beforeDb] {
            runOnMessageThread([&] { vp->setVolumeDb(beforeDb); });
            daw::v1::Event e;
            e.mutable_track_mixer_changed()->set_track_id(track_id);
            emitEvent(std::move(e));
        });

        emitCommandApplied(commit_id, description);
        fillMutationResult(response, commit_id, description);
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

        // Same capture-replay pattern as SetTrackVolume.
        const auto commit_id = makeCommitId();
        const std::string track_id = track->itemID.toString().toStdString();
        const float beforePan = vp->getPan();
        const std::string description = "Set track pan to " + std::to_string(panValue);

        runOnMessageThread([&] { vp->setPan(panValue); });

        {
            daw::v1::Event e;
            e.mutable_track_mixer_changed()->set_track_id(track_id);
            emitEvent(std::move(e));
        }

        pushUndo(commit_id, description, [this, vp, track_id, beforePan] {
            runOnMessageThread([&] { vp->setPan(beforePan); });
            daw::v1::Event e;
            e.mutable_track_mixer_changed()->set_track_id(track_id);
            emitEvent(std::move(e));
        });

        emitCommandApplied(commit_id, description);
        fillMutationResult(response, commit_id, description);
        return grpc::Status::OK;
    }

    grpc::Status Undo(grpc::ServerContext* /*context*/,
                      const daw::v1::UndoRequest* /*request*/,
                      daw::v1::MutationResult* response) override {
        // commit_id-targeted undo is in the proto contract but not supported
        // yet — our log is LIFO. Selective undo would require separating
        // dependent commits, which the agent doesn't need today. v2 fodder.
        UndoEntry entry;
        {
            std::lock_guard<std::mutex> lock(undo_log_mutex_);
            if (undo_log_.empty()) {
                return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                    "Nothing to undo");
            }
            entry = std::move(undo_log_.back());
            undo_log_.pop_back();
        }

        // The revert closure also emits the inverse type-specific event
        // (e.g., TrackRenamed back to the old name).
        entry.revert();

        // Then announce the undo itself so the agent knows what just
        // happened, with the original commit's id and description.
        {
            daw::v1::Event e;
            auto* cu = e.mutable_command_undone();
            cu->set_commit_id(entry.commit_id);
            cu->set_description(entry.description);
            emitEvent(std::move(e));
        }

        fillMutationResult(response, makeCommitId(), "Undo: " + entry.description);
        return grpc::Status::OK;
    }

    // -------- Transport --------
    //
    // Play/Stop are simple message-thread calls into Tracktion's
    // TransportControl. We don't emit a TransportStateChanged event yet —
    // the UI updates optimistically. Once a second caller (e.g. the agent)
    // can race with the UI we'll plumb the event through.

    grpc::Status Play(grpc::ServerContext* /*context*/,
                      const google::protobuf::Empty* /*request*/,
                      daw::v1::MutationResult* response) override {
        auto& transport = edit_.getTransport();
        const auto commit_id = makeCommitId();
        const bool wasPlaying = transport.isPlaying();
        const double positionSec = transport.getPosition().inSeconds();

        char buf[64];
        std::snprintf(buf, sizeof(buf), "Started playback at %.3fs", positionSec);
        const std::string description = buf;

        runOnMessageThread([&] { transport.play(false); });

        // Undo: if we actually started playback, stop. If transport was
        // already playing, undo is a no-op (we don't want to stop something
        // that was already running before the caller's command).
        if (! wasPlaying) {
            pushUndo(commit_id, description, [this] {
                runOnMessageThread([this] { edit_.getTransport().stop(false, false); });
            });
        } else {
            pushUndo(commit_id, description, []{});
        }

        emitCommandApplied(commit_id, description);
        fillMutationResult(response, commit_id, description);
        return grpc::Status::OK;
    }

    grpc::Status Stop(grpc::ServerContext* /*context*/,
                      const google::protobuf::Empty* /*request*/,
                      daw::v1::MutationResult* response) override {
        auto& transport = edit_.getTransport();
        const auto commit_id = makeCommitId();
        const bool wasPlaying = transport.isPlaying();
        const std::string description = "Stopped playback";

        // discardRecordings=false: keep any in-progress recording's audio.
        // clearDevices=false: don't tear down the playback graph; we'll
        // restart soon.
        runOnMessageThread([&] { transport.stop(false, false); });

        if (wasPlaying) {
            pushUndo(commit_id, description, [this] {
                runOnMessageThread([this] { edit_.getTransport().play(false); });
            });
        } else {
            pushUndo(commit_id, description, []{});
        }

        emitCommandApplied(commit_id, description);
        fillMutationResult(response, commit_id, description);
        return grpc::Status::OK;
    }

    grpc::Status GetTransportState(grpc::ServerContext* /*context*/,
                                   const google::protobuf::Empty* /*request*/,
                                   daw::v1::TransportState* response) override {
        auto& transport = edit_.getTransport();
        response->set_playing(transport.isPlaying());
        response->set_recording(transport.isRecording());
        // Position: seconds only for now. The proto's TimePosition.bars is
        // left at 0 — the UI doesn't render bars live yet, and computing
        // them needs a tempo-sequence lookup we don't have a helper for.
        setProtoTime(response->mutable_position(), transport.getPosition());
        return grpc::Status::OK;
    }

    // -------- Plugins --------

    grpc::Status RescanPlugins(grpc::ServerContext* /*context*/,
                               const google::protobuf::Empty* /*request*/,
                               daw::v1::MutationResult* response) override {
        // We deliberately do NOT acquire the MessageManagerLock here. A full
        // VST3+AU scan can take 30-120s on macOS (Gatekeeper validates each AU
        // in a subprocess). Holding the message-manager lock that long would
        // block every other RPC handler and stall the dispatch pump. JUCE's
        // KnownPluginList does its own internal locking, so concurrent reads
        // (ListAvailablePlugins, etc.) remain safe.
        auto& pm = engine_.getPluginManager();
        const auto deadMans = engine_.getPropertyStorage().getAppCacheFolder()
                                   .getChildFile("ClawdawDeadMans");

        bool aborted = false;
        for (auto* fmt : pm.pluginFormatManager.getFormats()) {
            if (fmt == nullptr) continue;
            if (fmt->getName() == te::PluginManager::builtInPluginFormatName) continue;
            if (g_shutdown_requested.load()) { aborted = true; break; }

            juce::PluginDirectoryScanner scanner(pm.knownPluginList, *fmt,
                                                 fmt->getDefaultLocationsToSearch(),
                                                 /*recurse=*/true, deadMans);
            juce::String currentName;
            while (scanner.scanNextFile(/*dontRescanIfAlreadyInList=*/true, currentName)) {
                std::cerr << "Scanning " << fmt->getName() << ": " << currentName << "\n";
                // Bail out promptly if the engine is shutting down. Without
                // this, a Ctrl+C during scan would block server->Shutdown()
                // until the entire format finishes — could be many minutes.
                if (g_shutdown_requested.load()) { aborted = true; break; }
            }
            if (aborted) break;
        }
        std::cerr << (aborted ? "Scan aborted: " : "Scan complete: ")
                  << pm.knownPluginList.getNumTypes() << " plugins known.\n";

        // Force the underlying PropertiesFile (which buffers writes on a
        // timer) to flush to disk so the scan cache survives the next
        // process restart. Without this, a SIGINT shortly after a long
        // scan loses the results.
        engine_.getPropertyStorage().getPropertiesFile().saveIfNeeded();

        if (aborted) {
            return grpc::Status(grpc::StatusCode::ABORTED, "Scan aborted by shutdown");
        }
        fillMutationResult(response, makeCommitId(), "Rescanned plugins");
        return grpc::Status::OK;
    }

    grpc::Status ListAvailablePlugins(grpc::ServerContext* /*context*/,
                                      const google::protobuf::Empty* /*request*/,
                                      daw::v1::ListAvailablePluginsResponse* response) override {
        // KnownPluginList::getTypes returns a copy of the internal array, so
        // no lock is needed. Built-in plugins (volume, level meter, etc.) are
        // not in this list — they're created implicitly by Tracktion when an
        // Edit is built and don't go through the scanner.
        for (const auto& d : engine_.getPluginManager().knownPluginList.getTypes()) {
            fillPluginInfo(response->add_plugins(), d);
        }
        return grpc::Status::OK;
    }

    grpc::Status AddPlugin(grpc::ServerContext* /*context*/,
                           const daw::v1::AddPluginRequest* request,
                           daw::v1::AddPluginResponse* response) override {
        auto* track = findTrackById(edit_, request->track_id());
        if (track == nullptr) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Track not found: " + request->track_id());
        }
        if (request->plugin_id().empty()) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "plugin_id required");
        }

        const juce::String wantedID(request->plugin_id());
        juce::PluginDescription found;
        bool foundIt = false;
        for (const auto& d : engine_.getPluginManager().knownPluginList.getTypes()) {
            if (d.createIdentifierString() == wantedID) {
                found = d;
                foundIt = true;
                break;
            }
        }
        if (! foundIt) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Unknown plugin_id: " + request->plugin_id());
        }

        // Note on slot semantics: proto `uint32 slot = 3` defaults to 0, so a
        // client that omits it is asking for slot 0 (first in the chain).
        // Tracktion's PluginList::insertPlugin treats out-of-range indices
        // as "append", so passing the proto value through directly gives the
        // user a way to append by sending a deliberately large slot. The
        // proto's "omit/-1 = append" comment doesn't translate cleanly to
        // unsigned ints — flagged for v2 cleanup.
        //
        // We use callAsync + a future here instead of MessageManagerLock.
        // AU plugin instantiation on macOS dispatches to internal queues that
        // need the message thread to be ACTIVELY running, not just held by
        // a worker thread. With MessageManagerLock the worker thread holds
        // the lock and the message thread is paused, so AU instantiation
        // deadlocks (observed: 60s+ hangs on every AU we tried). With
        // callAsync, the work runs on the actual message thread and the
        // dispatch loop continues processing internal AU/JUCE messages
        // around it.
        std::promise<te::Plugin::Ptr> pluginPromise;
        auto pluginFuture = pluginPromise.get_future();
        const int slot = (int) request->slot();
        juce::MessageManager::callAsync([this, track, slot, found, &pluginPromise]() mutable {
            edit_.getUndoManager().beginNewTransaction("Add plugin");
            auto p = edit_.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, found);
            if (p) {
                track->pluginList.insertPlugin(p, slot, nullptr);
            }
            pluginPromise.set_value(p);
        });
        te::Plugin::Ptr plugin = pluginFuture.get();

        if (! plugin) {
            return grpc::Status(grpc::StatusCode::INTERNAL,
                                "Plugin instantiation failed");
        }

        const auto commit_id = makeCommitId();
        const std::string track_id = track->itemID.toString().toStdString();
        const std::string instance_id = plugin->itemID.toString().toStdString();
        const std::string description = "Added plugin: " + plugin->getName().toStdString();

        {
            daw::v1::Event e;
            auto* pa = e.mutable_plugin_added();
            pa->set_plugin_instance_id(instance_id);
            pa->set_track_id(track_id);
            emitEvent(std::move(e));
        }

        // Capture the plugin we just inserted; revert removes it from its
        // parent. Same callAsync-based dispatch as the instantiation —
        // AU teardown has the same async-dispatch sensitivities as init.
        pushUndo(commit_id, description, [this, plugin, instance_id] {
            runOnMessageThreadSync([plugin] { plugin->removeFromParent(); });
            daw::v1::Event e;
            e.mutable_plugin_removed()->set_plugin_instance_id(instance_id);
            emitEvent(std::move(e));
        });

        emitCommandApplied(commit_id, description);
        response->set_plugin_instance_id(instance_id);
        fillMutationResult(response->mutable_mutation(), commit_id, description);
        return grpc::Status::OK;
    }

    grpc::Status GetPluginParameters(grpc::ServerContext* /*context*/,
                                     const daw::v1::GetPluginParametersRequest* request,
                                     daw::v1::GetPluginParametersResponse* response) override {
        auto plugin = findPluginById(edit_, request->plugin_instance_id());
        if (! plugin) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Plugin not found: " + request->plugin_instance_id());
        }
        for (auto* p : plugin->getAutomatableParameters()) {
            if (p == nullptr) continue;
            fillPluginParameter(response->add_parameters(), *p);
        }
        return grpc::Status::OK;
    }

    grpc::Status SetPluginParameter(grpc::ServerContext* /*context*/,
                                    const daw::v1::SetPluginParameterRequest* request,
                                    daw::v1::MutationResult* response) override {
        auto plugin = findPluginById(edit_, request->plugin_instance_id());
        if (! plugin) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Plugin not found: " + request->plugin_instance_id());
        }
        if (request->param_id().empty()) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "param_id required");
        }

        auto param = plugin->getAutomatableParameterByID(juce::String(request->param_id()));
        if (! param) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Param not found: " + request->param_id());
        }

        // Same capture-replay pattern as SetTrackVolume. We always store
        // the native value (setParameter takes native) regardless of which
        // oneof case the request used.
        const auto commit_id = makeCommitId();
        const std::string instance_id = request->plugin_instance_id();
        const std::string param_id = request->param_id();
        const float beforeValue = param->getCurrentValue();
        bool valueWasSet = false;
        runOnMessageThread([&] {
            switch (request->value_case()) {
                case daw::v1::SetPluginParameterRequest::kNativeValue:
                    param->setParameter((float) request->native_value(), juce::sendNotification);
                    valueWasSet = true;
                    break;
                case daw::v1::SetPluginParameterRequest::kNormalized:
                    param->setNormalisedParameter((float) request->normalized(), juce::sendNotification);
                    valueWasSet = true;
                    break;
                default:
                    break;  // neither set; leave param alone.
            }
        });

        const std::string description = "Set " + param->getParameterName().toStdString();

        if (valueWasSet) {
            const float afterValue = param->getCurrentValue();
            {
                daw::v1::Event e;
                auto* pp = e.mutable_plugin_param_changed();
                pp->set_plugin_instance_id(instance_id);
                pp->set_param_id(param_id);
                pp->set_new_value(afterValue);
                emitEvent(std::move(e));
            }

            pushUndo(commit_id, description, [this, param, instance_id, param_id, beforeValue] {
                runOnMessageThread([&] {
                    param->setParameter(beforeValue, juce::sendNotification);
                });
                daw::v1::Event e;
                auto* pp = e.mutable_plugin_param_changed();
                pp->set_plugin_instance_id(instance_id);
                pp->set_param_id(param_id);
                pp->set_new_value(beforeValue);
                emitEvent(std::move(e));
            });

            emitCommandApplied(commit_id, description);
        }
        // Only log an undo if we actually changed something — otherwise we'd
        // pollute the stack with a no-op revert.

        fillMutationResult(response, commit_id, description);
        return grpc::Status::OK;
    }

    // -------- Events --------

    grpc::Status SubscribeEvents(grpc::ServerContext* context,
                                 const daw::v1::EventFilter* request,
                                 grpc::ServerWriter<daw::v1::Event>* writer) override {
        auto sub = broadcaster_.subscribe(*request);

        // Per-event poll loop. Wakes on either (a) a new event in the queue
        // (via cv.notify_one inside broadcaster.broadcast), or (b) the 200ms
        // timeout, which is how we re-check ctx->IsCancelled and the global
        // shutdown flag without the broadcaster having to know about either.
        // Subscriber teardown is implicit: we hold the only shared_ptr to the
        // Subscription, and when this function returns the broadcaster's weak
        // ref expires.
        while (! context->IsCancelled() && ! g_shutdown_requested.load()) {
            daw::v1::Event evt;
            bool got = false;
            {
                std::unique_lock<std::mutex> lock(sub->queue_mutex);
                sub->cv.wait_for(lock, std::chrono::milliseconds(200), [&] {
                    return ! sub->queue.empty();
                });
                if (! sub->queue.empty()) {
                    evt = std::move(sub->queue.front());
                    sub->queue.pop_front();
                    got = true;
                }
            }
            if (got && ! writer->Write(evt)) break;  // client disconnected
        }
        return grpc::Status::OK;
    }

private:
    // Undo log. Each mutation pushes a revert closure here in execution
    // order; Undo pops the most recent and runs it.
    //
    // We deliberately do NOT use Tracktion's UndoManager. Two reasons:
    //   1. AutomatableParameter::valueTreePropertyChanged explicitly does
    //      not propagate ValueTree-undo changes back to the parameter's
    //      runtime currentValue (the audio-thread value), so an
    //      undoManager.undo() of a volume change reverts the persistent
    //      CachedValue but leaves the audio path running with the old
    //      value. Documented at tracktion_AutomatableParameter.cpp:882-892.
    //   2. setVolumeDb / setPan / setParameter still write through the
    //      UndoManager via the bound CachedValue, so each side-band revert
    //      we'd record creates a NEW UndoManager transaction. Mixing the
    //      two systems leaves the UndoManager stack polluted in ways that
    //      break interleaved rename + param sequences (the rename revert's
    //      undo() pops a spurious vol-revert transaction instead).
    //
    // So: capture the before-state of every mutation explicitly, and let
    // Tracktion's UndoManager fill up unused. Undo replays the captured
    // before-state through the same setter the original mutation used,
    // which keeps the parameter, CachedValue, and ValueTree all aligned.
    //
    // Bounded so a long-running session doesn't grow unbounded; older
    // entries are dropped (and become un-undoable). 100 is plenty for v1.
    static constexpr size_t kMaxUndoEntries = 100;

    struct UndoEntry {
        std::string commit_id;       // of the original mutation
        std::string description;     // shown in CommandUndone payload
        std::function<void()> revert; // does revert + emits inverse type-specific event
    };
    std::mutex undo_log_mutex_;
    std::deque<UndoEntry> undo_log_;

    void pushUndo(std::string commit_id, std::string description,
                  std::function<void()> revert) {
        std::lock_guard<std::mutex> lock(undo_log_mutex_);
        undo_log_.push_back({std::move(commit_id), std::move(description), std::move(revert)});
        while (undo_log_.size() > kMaxUndoEntries) {
            undo_log_.pop_front();
        }
    }

    // Build a CommandApplied event and broadcast it. The type-specific event
    // (TrackRenamed, etc.) is emitted by the handler itself so the caller
    // controls its exact contents; this helper just emits the bookkeeping
    // event the agent uses for "what just happened?".
    void emitCommandApplied(const std::string& commit_id, const std::string& description) {
        daw::v1::Event e;
        e.set_event_id(makeCommitId());
        auto* ca = e.mutable_command_applied();
        ca->set_commit_id(commit_id);
        ca->set_description(description);
        ca->set_source("agent");  // we don't distinguish user vs agent yet
        broadcaster_.broadcast(e);
    }

    void emitEvent(daw::v1::Event e) {
        if (e.event_id().empty()) e.set_event_id(makeCommitId());
        broadcaster_.broadcast(e);
    }

public:
    EventBroadcaster& broadcaster() { return broadcaster_; }

private:
    EventBroadcaster broadcaster_;
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

    // PluginManager::initialise() registers VST3/AU formats with the
    // pluginFormatManager and restores the on-disk scan cache. Required
    // before RescanPlugins, AddPlugin, or anything else that touches
    // knownPluginList — Tracktion asserts on the first such call otherwise.
    engine.getPluginManager().initialise();

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

    // Flush any pending property writes (notably the plugin scan cache) so
    // they're durable before we exit.
    engine.getPropertyStorage().getPropertiesFile().saveIfNeeded();

    // Wake any blocked SubscribeEvents handler threads so they re-check
    // g_shutdown_requested and return — otherwise server->Shutdown() waits
    // on them and we hang for up to 200ms per subscriber.
    service.broadcaster().wakeAll();

    server->Shutdown();
    grpc_thread.join();
    return 0;
}
