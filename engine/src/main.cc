#include <iostream>
#include <memory>
#include <string>

#include <google/protobuf/empty.pb.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <tracktion_engine/tracktion_engine.h>

#include "daw/v1/engine.grpc.pb.h"
#include "daw/v1/project.pb.h"
#include "daw/v1/common.pb.h"

namespace te = tracktion::engine;

namespace {

// Map a Tracktion Track to our proto TrackType. In Tracktion, MIDI tracks
// are AudioTracks (the audio track holds both MIDI and audio clips), so we
// can't distinguish MIDI vs audio by class — that's a regions/clips question
// to answer later.
daw::v1::TrackType protoTrackType(const te::Track& track) {
    if (track.isMasterTrack()) return daw::v1::TRACK_TYPE_BUS;
    if (track.isFolderTrack()) return daw::v1::TRACK_TYPE_FOLDER;
    if (track.isAudioTrack())  return daw::v1::TRACK_TYPE_AUDIO;
    return daw::v1::TRACK_TYPE_UNSPECIFIED;
}

class EngineServiceImpl final : public daw::v1::Engine::Service {
public:
    explicit EngineServiceImpl(te::Edit& edit) : edit_(edit) {}

    grpc::Status ListTracks(grpc::ServerContext* /*context*/,
                            const google::protobuf::Empty* /*request*/,
                            daw::v1::ListTracksResponse* response) override {
        for (auto* track : te::getAllTracks(edit_)) {
            auto* summary = response->add_tracks();
            summary->set_id(track->itemID.toString().toStdString());
            summary->set_name(track->getName().toStdString());
            summary->set_type(protoTrackType(*track));
            summary->set_muted(track->isMuted(false));
            summary->set_soloed(track->isSolo(false));
            // plugin_count and region_count: filled in once we wire those subsystems.
        }
        return grpc::Status::OK;
    }

private:
    te::Edit& edit_;
};

}  // namespace

int main(int argc, char** argv) {
    const std::string address = (argc > 1) ? argv[1] : "127.0.0.1:50051";

    // Tracktion's Engine constructor sets up the JUCE message manager and
    // application name; the resulting object owns plugin scanning, device
    // management, and the engine-wide settings. One per process.
    te::Engine engine{"CLAWDAW"};

    // Create an in-memory Edit (createSingleTrackEdit doesn't need a backing
    // file path) and pad it to 2 tracks so ListTracks returns visible data.
    auto edit = te::Edit::createSingleTrackEdit(engine);
    edit->ensureNumberOfAudioTracks(2);

    EngineServiceImpl service{*edit};

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    if (!server) {
        std::cerr << "Failed to start gRPC server on " << address << "\n";
        return 1;
    }

    std::cout << "clawdaw_engine listening on " << address
              << " (tracks in edit: " << te::getAllTracks(*edit).size() << ")\n";

    server->Wait();
    return 0;
}
