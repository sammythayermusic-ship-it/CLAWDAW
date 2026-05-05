# CLAWDAW — Session Log

Append-only log of each work session. Newest at the top.

---

## 2026-05-05 (continued) — Phase 2 first cut: gRPC server + first RPC end-to-end ✅

**Goal:** Stand up the gRPC layer in the engine. Get one read-side RPC (`ListTracks`) running end-to-end against a real Tracktion `Edit`.

**Outcome:** Working. `clawdaw_engine` binary starts a gRPC server with reflection on `127.0.0.1:50051`, instantiates a Tracktion `Engine` and an in-memory `Edit`, and `ListTracks` returns the Edit's actual track list (7 tracks: Arranger, Chord, Marker, Tempo, Master, Track 1, Track 2 — last three are the audio + bus tracks; first four are Tracktion's auto-created system tracks).

### What was built

- Repo reorganized to match the layout CLAUDE.md describes: protos at [proto/daw/v1/](proto/daw/v1/), buf configs at [proto/](proto/), planning doc at [docs/daw-build-planning.md](docs/daw-build-planning.md). Old proto README and planning doc moved out of project root.
- Tooling installed via Homebrew: `buf 1.69`, `grpc 1.80`, `protobuf 34.1`, `abseil 20260107`, `grpcurl`.
- `buf generate` produces 32 C++ stub files (`*.pb.cc/h` + `*.grpc.pb.cc/h`) in [engine/generated/daw/v1/](engine/generated/daw/v1/). Generated code is currently committed (will switch to a CMake-driven regen step later).
- [engine/CMakeLists.txt](engine/CMakeLists.txt) builds a static `clawdaw_proto` library from the generated sources, links `protobuf::libprotobuf`, `gRPC::grpc++`, `gRPC::grpc++_reflection`, and brings in Tracktion via `add_subdirectory(third_party/tracktion_engine EXCLUDE_FROM_ALL)` so Tracktion's example targets stay out of the default build.
- [engine/src/main.cc](engine/src/main.cc) — minimal sync gRPC server with reflection. Constructs a `te::Engine{"CLAWDAW"}` (which sets up JUCE's message manager) and an `Edit::createSingleTrackEdit(engine)` then `ensureNumberOfAudioTracks(2)`. The `ListTracks` impl iterates `te::getAllTracks(*edit)` and populates `TrackSummary` from each track's actual ID, name, type, mute, solo.
- Engine builds with `cmake --build engine/build --target clawdaw_engine -j 8`. Clean build (Tracktion library targets + JUCE modules + our code), only deployment-target warnings (Tracktion targets macOS-11, Homebrew libs are macOS-26 — warnings only, runs fine).

### What worked / what failed first try

- **First main.cc draft had several Tracktion API guesses wrong** — `createEmptyEdit(engine)` returns a `juce::ValueTree`, not `unique_ptr<Edit>` (the file-taking overload returns the unique_ptr). `isMidiTrack()` doesn't exist (in Tracktion, MIDI tracks are AudioTracks too — they hold MIDI clips). `engine.getApplicationName()` doesn't exist. Fixed by greping headers for actual signatures (`Edit::createSingleTrackEdit`, `te::getAllTracks`, the documented `is*Track()` predicates) and patching main.cc.
- **buf lint exits 100** with ~120 style warnings (e.g., service should be named `EngineService`, every RPC should have its own request/response message rather than reusing `MutationResult` and `Project`). These are stylistic and don't block `buf generate`. The proto contract is locked, so we won't change them — adding the offending rule names to the `except:` list in [proto/buf.yaml](proto/buf.yaml) is a small follow-up.
- **Engine startup worked first try after the API fixes.** Tracktion printed `Settings file: /Users/samthayer/Library/CLAWDAW/Settings.xml`, `Audio block size: 512  Rate: 48000`, `Creating Default Controllers...` — it created its app-scoped settings dir and initialized the device manager. No crash, no JUCE message-loop drama for read-only RPCs. We may need a dedicated message thread later for mutating RPCs.

### Known limitations / follow-ups carried forward

- **`TrackType` enum doesn't cover Arranger / Chord / Marker / Tempo tracks.** Tracktion auto-creates these on every Edit; they currently come back with `TRACK_TYPE_UNSPECIFIED`. Either filter them out of `ListTracks` (treat as "system") or extend the proto enum (`TRACK_TYPE_ARRANGER`, `TRACK_TYPE_MARKER`, etc.). Affects [proto/daw/v1/common.proto](proto/daw/v1/common.proto). Because v1 is supposed to be locked, prefer filtering for now and adding fields in v2.
- **Audio vs MIDI track distinction.** `isAudioTrack()` returns true for both because Tracktion stores MIDI clips on AudioTracks. We'd answer "is this MIDI" by checking what kind of clips it contains. Not done yet.
- **`plugin_count` and `region_count` in `TrackSummary` are always 0.** Filled in once we wire those subsystems (next session, alongside `GetTrack`).
- **Generated code is committed** (`engine/generated/`). Standard practice is to regenerate from protos during build. Add a CMake `add_custom_command` that runs `buf generate` when any `.proto` changes.
- **JUCE message-loop strategy for mutating RPCs.** Read-only `ListTracks` works without us managing a message loop, but writes (`SetTrackVolume`, `AddPlugin`, etc.) probably need to be marshalled to JUCE's message thread. Decide before implementing first mutation.
- **Deployment target mismatch warnings.** Tracktion's CMakeLists pins macOS-11 deployment target; Homebrew bottles are macOS-26. Linker warns. Fix by either bumping Tracktion's target or downgrading Homebrew formulas to a matching version. Defer; warnings only.
- **Tarball-based dependencies still not converted to submodules.** Same caveat as Phase 1 entry below.

### What's next

1. **`GetTrack(track_id)`** — given an ID, return the full `Track` message (mixer state, plugin list, regions). Most fields are already on Tracktion's `Track` / `AudioTrack`; just translation work.
2. **`GetProject()`** — return `Project` (tempo, key, sample rate, length, the same `TrackSummary` list, markers). Tracktion exposes most of this on `Edit::tempoSequence` etc.
3. **`SetTrackVolume`, `SetTrackPan`, `RenameTrack`** — first mutating RPCs. Need to figure out JUCE message-thread marshalling; design choice point.
4. **`AddPlugin`, `SetPluginParameter`, `GetPluginParameters`** — first plugin-touching RPCs. May need to call Tracktion's plugin scanner first; consider a separate `RescanPlugins` warm-up.
5. **`Undo`** — Tracktion has its own `UndoManager`. Wire `MutationResult.commit_id` to a label on the undo entry, expose `Undo(commit_id)` via the manager.

That's the first 10 RPCs, finishing Phase 2. Realistic to do over 2-3 sessions.

### Repo state at end of this session

- New top-level dirs: `proto/`, `docs/` (existing files moved in), `engine/src/`, `engine/generated/`.
- Built artifacts under `engine/build/` (gitignored).
- Tracktion's CMake-driven Phase-1 build dir `engine/third_party/tracktion_engine/cmake-build/` still on disk from yesterday's foundation validation; safe to delete (we don't use it from our own CMake).
- Still no commits in the repo. The growing pile of changes is a fine first commit when ready.

---

## 2026-05-05 — Phase 1 foundation validation ✅

**Goal:** Get Tracktion Engine's example app building and running on the dev Mac. Record audio from the interface, play it back. Decide if the architecture is viable on this hardware.

**Outcome:** Foundation validated. Phase 1 milestones 1, 2, 3, and 8 pass (engine compiles + runs, audio plays from a file, transport works, audio records from input and plays back). **Ready to move to Phase 2 (gRPC layer in the engine) next session.**

### What worked

- Tracktion Engine (master, commit `b0c7dd09`) + JUCE (commit `19edd538`, Feb 3 2025) built cleanly with CMake's Xcode generator on macOS 26.3.1, Xcode 26.3, Apple Silicon (arm64).
- `cmake --preset xcode` generated a clean `tracktion.xcodeproj` with `DemoRunner` scheme.
- `xcodebuild -scheme DemoRunner -configuration Debug` with ad-hoc signing produced a working `DemoRunner.app`. 36 source files compiled, 0 errors. Build time ~3 min on M-series.
- `DemoRunner.app` launched, PlaybackDemo played audio through MacBook Pro speakers, RecordingDemo prompted for microphone access (Info.plist `NSMicrophoneUsageDescription` was correctly declared as "Audio recording") and successfully recorded + played back from the built-in mic.

### What didn't (and what we did instead)

- **Git submodule clones to GitHub failed twice.** Connection kept getting reset (`curl 28 Operation too slow`, `RPC failed; curl 56 Connection reset by peer`) at 100KB/s and worse. Network-side issue, not a fix-by-flag issue. Fell back to **direct tarball downloads** via `curl -L -C - --retry 20` — slower per-byte than fast paths but byte-range-resumable, so reliable.
  - Consequence: `engine/third_party/tracktion_engine/` is currently a tarball-extracted directory, not a tracked git submodule. Convert to a proper submodule next time the network cooperates.
- **JUCE develop branch was incompatible with Tracktion master.** First build failed with `error: allocating an object of abstract class type 'FloatAudioFormat'` in `tracktion_TestUtilities.h:686` — JUCE develop made `FloatAudioFormat` abstract; Tracktion still instantiates it directly. Tracktion master pins JUCE to a specific commit (`19edd538`, Feb 3 2025) that is **886 commits behind current JUCE develop**. Re-downloaded JUCE at the pinned sha and re-built.
- **Homebrew install partially failed.** The main install completed (brew binary + portable-ruby), but the final `brew update --force --quiet` step failed with `Failed to download https://formulae.brew.sh/api/formula.jws.json`. Bottle downloads from `ghcr.io` worked fine, so `brew install cmake` succeeded directly without needing a full formula refresh. Re-run `brew update` later when network is better.
- **`cmake-build/` had stale state** after swapping JUCE source under it. ZERO_CHECK reran cmake but juceaide's test failed inexplicably (its binary worked when run directly). Fix was nuking `cmake-build/` entirely and re-running `cmake --preset xcode` from scratch. Lesson: when you change JUCE/Tracktion source, fully clean the build dir, don't try to incrementally re-configure.
- **First `xcodebuild` background command reported exit 0 despite the build failing.** Cause: piping through `| tail -30` masked xcodebuild's exit code. Subsequent runs used `> log 2>&1; echo "EXIT=$?"` to get the real exit code. Useful pattern to remember.
- **The git repo at `/Users/samthayer/Documents/.git`** (rooted at Documents, empty, no commits) was confusing `git status` from inside the project. Initialized a proper project-scoped repo at `/Users/samthayer/Documents/Claude/Projects/CLAWDAW/.git`. The Documents-rooted one was left alone (zero content, harmless).

### Repo state at end of session

- Project-scoped git repo initialized at `CLAWDAW/`. **No commits yet.** Sammy to make the initial commit when ready.
- New top-level files/dirs (untracked):
  - `.gitignore` — covers Mac, Xcode, CMake build outputs, third-party build dirs.
  - `engine/third_party/_downloads/` — Tracktion + JUCE-pinned tarballs (~70MB total). Safe to delete; kept around in case we want to re-extract.
  - `engine/third_party/tracktion_engine/` — extracted Tracktion source with JUCE-pinned at `modules/juce/`.
  - `engine/third_party/tracktion_engine/cmake-build/Darwin/cmake-xcode/` — generated Xcode project + build outputs (gitignored). DemoRunner.app lives under `examples/DemoRunner/DemoRunner_artefacts/Debug/`.
  - `docs/session-log.md` — this file.
- Note: the layout described in CLAUDE.md (`docs/`, `proto/daw/v1/`) doesn't yet match disk reality. The `.proto` files and `daw-build-planning.md` are still flat in the project root from before this session. Tidying them is a separate cleanup task.

### What's next (Phase 2)

Per CLAUDE.md, Phase 2 is implementing the gRPC layer in the engine. Concretely for the next session:

1. Create `engine/src/` with our own C++ entry point and a small CMakeLists.txt that links against Tracktion as a library (rather than building inside their tree).
2. Generate gRPC C++ stubs from the protos in the project root via `buf generate` — first need to install `buf` (`brew install bufbuild/buf/buf`) and `protoc` plugins.
3. Implement the read-side RPCs first: `GetProject`, `GetTrack`, `ListTracks`. Just stand up the gRPC server, return stubbed data initially, then wire to a real Tracktion `Edit`.
4. Then the basic mutations: `AddPlugin`, `SetPluginParameter`, `GetPluginParameters`, `SetTrackVolume`, `SetTrackPan`, `RenameTrack`, `Undo`. That's the first-10 set from the planning doc.

### Open questions / housekeeping carried forward

- **Submodules vs tarballs:** convert `engine/third_party/tracktion_engine/` to a proper submodule next time GitHub clones are reliable from this network. Document the pinned JUCE sha somewhere we can update (probably a top-level `engine/CMakeLists.txt` that supersedes Tracktion's bundled one, or a script that pins the inner submodule explicitly).
- **JUCE/Tracktion version drift:** Tracktion master pins year-old JUCE. If we want a newer JUCE feature, we'll need to either wait for Tracktion to bump, fork their submodule, or carry a small patch. Decide before relying on bleeding-edge JUCE.
- **Code signing:** ad-hoc signing works for local dev but won't suffice once we ship anything that touches Audio Units (entitlements + Developer ID required). Defer to whenever distribution is on the table.
- **Audio interface:** session brief said "An audio interface is connected" but `system_profiler SPAudioDataType` only showed MacBook Pro built-in mic/speakers, an iPhone Continuity mic, and an "Immersed" virtual driver. RecordingDemo recorded fine from built-in mic, but if you want to validate with the real interface, plug it in next session.
- **Initial commit:** repo has no commits yet. When you're ready, the obvious initial commit is the existing planning docs + protos + `.gitignore`. Tarballs and `cmake-build/` are gitignored.
- **Homebrew formula list is stale.** Run `brew update` later when network's healthier. Doesn't block anything immediate.

### Time spent

~5 hours including network failures and re-strategy. Bulk of the time was the JUCE-version-mismatch debug cycle and the cmake-build state confusion — both filed in feedback memory so we don't repeat them.
