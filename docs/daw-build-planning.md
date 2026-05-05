# DAW Build — Planning Document

This document covers three foundational topics in the order needed to start building:

1. Architecture & IPC design
2. MCP tool surface for the agent
3. Tracktion Engine "hello world"

---

## Part 1: Architecture & IPC Design

### The three-process model

Three separate processes communicate to make this work. Keeping them separate isn't bureaucratic — it's how you protect audio from glitching when the UI repaints or the agent runs an ML model.

**Audio Engine (C++)**
- Built on Tracktion Engine + JUCE
- Owns the audio graph, plugin instances, transport, automation
- Real-time priority on its audio thread
- Source of truth for anything affecting playback
- Exposes a control RPC server

**UI Process (Tauri + React + TypeScript)**
- Renders the interface, handles input
- Stateless — pulls state from engine via streaming RPC
- Sends commands via RPC
- Can crash without taking down playback

**Agent Process (Python)**
- Hosts the MCP server that Claude connects to
- Runs ML inference (instrument classification, stem separation, etc.)
- Talks to the engine via the same RPC layer the UI uses
- Can be killed and restarted independently

### Why three processes

The cardinal rule of audio software: **the audio thread must never be blocked.** No allocations, no locks, no file I/O, no network calls. Anything that could pause it for >5ms causes an audible glitch. Tracktion handles this internally if you respect its boundaries — but if your UI and agent live in the same process as the engine, one bad GC pause kills the audio.

Separation also gives you:
- Crash isolation (agent crash doesn't drop your recording)
- Right tool per layer (C++ for DSP, TS for UI, Python for ML)
- Independent deployment and updates
- Clean test boundaries

### IPC protocol

**Recommendation: gRPC + protobuf for the control plane, shared-memory ring buffers for audio data.**

gRPC gives you typed schemas, bidirectional streaming, and excellent cross-language support. You define the engine's API once in `.proto` files; UI and agent get generated clients. Latency over local sockets is sub-millisecond.

For audio data flowing engine → agent (so the agent can listen to tracks), gRPC works but is wasteful. Use a lock-free single-producer-single-consumer ring buffer in shared memory, and a control RPC that says "start streaming track 3 to ringbuffer X." This keeps the audio thread allocation-free.

```
+------------------+      gRPC (control)      +------------------+
|  Audio Engine    | <----------------------> |   UI Process     |
|  (C++/JUCE)      | <----------------------> |   (Tauri/React)  |
+------------------+                          +------------------+
        ^
        | gRPC (control) + shm ringbuffer (audio data)
        v
+------------------+         gRPC             +------------------+
|  Agent Process   | <----------------------> |   Claude (MCP)   |
|  (Python)        |                          +------------------+
+------------------+
```

### State model and command flow

The engine is the **single source of truth**. Everything the UI shows is derived from engine state.

Two communication patterns:

1. **Commands (UI/Agent → Engine):** mutating operations like `add_plugin`, `set_param`, `delete_region`. These go onto a single ordered command queue. The engine processes them on its message thread, applies them, then emits state diffs.

2. **State stream (Engine → UI/Agent):** a server-streaming RPC that emits diff events. UI applies diffs to its local state mirror. Agent does the same when it cares to track state.

Why command queue + diff stream rather than direct method calls?
- Naturally ordered (no race conditions between UI and agent)
- Automatically loggable (replay = undo + redo)
- The agent can subscribe to commands too — useful for "what did the user just do?" awareness

### Threading model

**Engine process:**
- 1 audio thread (real-time, runs the audio graph, never blocks)
- 1 message thread (drains the command queue, applies changes atomically)
- 1 disk I/O thread (recording, file writes)
- 1 RPC thread pool (handles incoming requests, posts work to message thread)

**UI process:** standard event loop.

**Agent process:** async event loop, ML inference on dedicated worker threads (PyTorch is fine; just don't block the request handler).

### Project file format

JSON or msgpack — both agent-readable. Resist binary formats; your agent needs to read this and so do you when debugging at 1am. Plugin state goes in as base64 (use the plugin's own `getStateInformation`/`setStateInformation`).

Versioning matters from day one. Write a `version` field and a migrator framework now, even if v0→v1 is trivial.

### Undo/redo as command pattern

Every mutation is a `Command` object with `apply()` and `revert()`. The engine maintains a stack. The agent gets undo/redo as tools too. **Non-negotiable:** if the agent applies 12 EQ changes you don't like, "undo" should peel them back as one batch or one at a time.

### Decisions to make in week 1

- gRPC vs WebSocket vs Cap'n Proto (gRPC is the default unless you hit a wall)
- JUCE built-in UI vs Tauri (Tauri for modern look; more glue work upfront)
- Single-binary install vs three-process bundle (three processes; ship them together)
- Plugin sandboxing strategy (Tracktion has options here; decide if a crashed plugin should kill the engine or just the plugin)
- Licensing — Tracktion Engine, JUCE, and the VST3 SDK each have terms worth reading early

---

## Part 2: MCP Tool Surface

### Design principles

1. **Right granularity.** Not "set parameter at memory address" (too fine, agent can't reason). Not "make it sound better" (too coarse, no execution path). Aim for what a human engineer would say: "EQ track 3, cut 4dB at 300Hz, Q=1.0."

2. **Reversible.** Every write tool returns a `commit_id` that `undo` can take. The agent should feel free to try things.

3. **Read before write.** Lots of read tools is good. The agent needs context to make decisions.

4. **Listening as first-class.** Some workflows ("auto-name when I record") are reactive. Streaming tools and event subscriptions need to exist.

5. **Safe defaults.** Destructive operations (delete, overwrite) require explicit confirmation flags.

### Tool surface (initial draft)

#### Project + transport
- `get_project()` → `Project { tempo, key, length_bars, tracks: [Track], markers: [Marker] }`
- `get_transport_state()` → `{ playing, recording, position_seconds, position_bars }`
- `play()`, `stop()`, `record()`, `loop(start, end)`
- `set_tempo(bpm)`, `set_key(root, scale)`

#### Tracks
- `list_tracks()` → `[TrackSummary]`
- `get_track(track_id)` → `Track { name, color, type, regions, plugins, mixer: { volume, pan, sends, mute, solo } }`
- `add_track(type, name?)` → `track_id`
- `delete_track(track_id, confirm=true)` → `commit_id`
- `rename_track(track_id, name)`
- `set_track_color(track_id, color)`
- `set_track_volume(track_id, db)`
- `set_track_pan(track_id, position)`

#### Plugins
- `list_available_plugins()` → `[PluginInfo { id, name, vendor, type: VST3|AU|internal, category }]`
- `add_plugin(track_id, plugin_id, slot_position?)` → `plugin_instance_id`
- `remove_plugin(plugin_instance_id, confirm=true)`
- `get_plugin_params(plugin_instance_id)` → `[{ id, name, value, normalized, min, max, unit, automatable }]`
- `set_plugin_param(plugin_instance_id, param_id, value)`
- `bypass_plugin(plugin_instance_id, bypassed)`
- `save_plugin_preset(plugin_instance_id, name)` / `load_plugin_preset(plugin_instance_id, name)`

#### Editing (regions/clips)
- `list_regions(track_id)`, `get_region(region_id)`
- `move_region(region_id, new_start)`
- `trim_region(region_id, start, length)`
- `cut_region(region_id, at_position)` → `[left_id, right_id]`
- `duplicate_region(region_id)`, `delete_region(region_id)`
- `set_region_gain(region_id, db)`

#### MIDI
- `get_midi_notes(region_id)` → `[MidiNote]`
- `add_midi_note(region_id, pitch, start, length, velocity)`
- `quantize(region_id, grid: 1/16, swing: 0.0)`
- `transpose(region_id, semitones)`

#### Mixing + automation
- `add_send(track_id, dest_track_id, level)` → `send_id`
- `add_automation(target_id, param_id, points: [(time, value)])`

#### Analysis (the listening layer)
- `analyze_track(track_id)` → `{ instrument, confidence, key, tempo, loudness_lufs, dynamic_range }`
- `analyze_region(region_id)` → same fields scoped to a region
- `get_spectrum(track_id, time_seconds)` → `[{ freq_hz, magnitude_db }]`
- `get_loudness(target, mode: "integrated"|"momentary")` → lufs
- `subscribe_to_track_audio(track_id)` → stream of audio frames (live listening)

#### Reactive events
- `subscribe_events(filter)` → stream of `{ type, payload }` where type is `record_complete`, `region_added`, `plugin_added`, `transport_state_changed`, etc.
- This is how the agent does background tasks: subscribe once, react as events fire.

#### Undo/history
- `undo(commit_id?)`, `redo()`
- `get_history(limit)` → `[{ commit_id, description, timestamp, by: "user"|"agent" }]`

#### External integrations (later phases)
- `splice_search(query, filters)` → `[Sample]`
- `splice_import(sample_id, target_track_id?)` → `region_id`
- `suno_generate(prompt, duration, target_track_id?)` → `region_id`

### Example agent workflows

**Auto-name on record.** Agent subscribes to `record_complete`. On fire: `analyze_track` → if confidence > 0.85, `rename_track` to the detected instrument; otherwise prompt user.

**"Make the mix punchier."** `get_project` → `analyze_track` for each → identify problems (e.g., bass and kick overlap at 80Hz, vocal sits 4dB low) → propose changes as a structured plan → user approves → `add_plugin` (EQ on bass), `set_plugin_param` (cut 80Hz), `set_track_volume` on vocal. Each change has a `commit_id`. "Undo all that" walks them back.

**"Find a snare like this."** User selects region → agent: `get_region` → `analyze_region` → use features as query → `splice_search` → present 5 samples → on selection → `splice_import`.

### What NOT to expose

- Direct memory or buffer access (the agent doesn't need it; encourages bad designs)
- File system writes outside the project folder
- Anything bypassing undo/redo

### Build order for the tool surface

Don't build all 40 tools at once. Ship the ones that unlock the first agent demo:

1. `get_project`, `get_track`, `list_tracks` (read state)
2. `add_plugin`, `set_plugin_param`, `get_plugin_params` (manipulate plugins)
3. `set_track_volume`, `set_track_pan`, `rename_track` (mixing basics)
4. `analyze_track` (the listening hook)
5. `undo`, `redo`, `get_history` (safety net)
6. `subscribe_events` (reactive workflows)

That's ten tools. With those, you can demo: "Claude, listen to track 3, name it, then mix it sensibly into the rest of the project." Everything else is incremental.

---

## Part 3: Tracktion Engine "Hello World"

The goal of this phase is to validate the foundation on your machine before committing to the architecture. Two evenings of work, max.

### Prerequisites

- Xcode (Mac) or Visual Studio 2022 (Windows)
- CMake 3.22+
- Git
- A VST3 plugin you own (any free one — Vital, TDR Nova, Surge XT)
- An audio file (WAV or AIFF, 44.1k or 48k)

### Setup

```bash
mkdir ~/daw-prototype && cd ~/daw-prototype

# Clone JUCE and Tracktion Engine
git clone https://github.com/juce-framework/JUCE.git
git clone https://github.com/Tracktion/tracktion_engine.git

# The Tracktion repo includes example projects — start there
cd tracktion_engine/examples
```

Open one of the example `.jucer` files (e.g., `RecordingDemo` or `EngineInPluginDemo`) in **Projucer** (you'll find it in `JUCE/extras/Projucer`). Set the JUCE module paths to your local clone, export to Xcode/VS, and build. Get the example running before writing any of your own code.

### Minimal "play a file through a VST" sketch

This is API-shape, not paste-and-compile — the working version uses JUCE's `JUCEApplication` boilerplate. Use the Tracktion examples as the actual starting template.

```cpp
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

void setupAndPlay()
{
    te::Engine engine { "DAWPrototype" };

    // Tracktion calls a project an "Edit"
    auto edit = te::createEmptyEdit(engine);

    // Add an audio track and load a file as a clip
    auto* track = te::getAudioTracks(*edit)[0];
    juce::File audioFile { "/Users/sammy/Music/test.wav" };
    track->insertWaveClip("test_clip", audioFile,
                          { { 0.0, 4.0 }, 0.0 }, false);

    // Scan for and add a VST3
    auto& pm = engine.getPluginManager();
    pm.knownPluginList.scanAndAddFile(
        "/Library/Audio/Plug-Ins/VST3/TDR Nova.vst3",
        true,
        pm.pluginFormatManager.getFormats(),
        true);

    auto desc = pm.knownPluginList.getTypes()[0];
    auto plugin = edit->getPluginCache().createNewPlugin(
        te::ExternalPlugin::xmlTypeName, desc);
    track->pluginList.insertPlugin(plugin, 0, nullptr);

    // Initialise audio device and roll
    engine.getDeviceManager().initialise();
    edit->getTransport().play(false);
}
```

### Validation milestones

Tick these off in order. Don't move past one until it works.

1. **Engine compiles and runs.** Empty Edit, no errors on startup or shutdown.
2. **Audio plays from a file** through your default output device. You can hear it.
3. **Transport works** — play, stop, seek programmatically.
4. **A VST3 loads** and shows up in the plugin list. You don't need to render its UI yet.
5. **Audio routes through the VST3.** Effect is audible.
6. **Save and load an Edit** to/from disk. Reopened project plays the same way.
7. **MIDI clip drives a synth VST.** Confirms the MIDI path works end to end.
8. **Record audio from your interface** to a new clip. This is where real-time correctness matters.
9. **Recall plugin state** — set some params, save, close, reopen, params are preserved.

### Common gotchas

- **VST3 SDK license.** Steinberg's license is permissive for personal/research use but has terms for commercial distribution. Read it before assuming.
- **AU plugins on Mac** require code signing and entitlements once you start distributing. For local prototyping you're fine.
- **Real-time thread rules.** Never allocate, lock, log, or do file I/O in `processBlock`. JUCE/Tracktion enforce this in many places, but you can still shoot yourself in the foot in callbacks.
- **Plugin scanning is slow.** Cache `knownPluginList` after first scan. Some plugins crash on scan; sandbox the scanner process.
- **Sample rate mismatches** between device and project will silently sound wrong. Match them, or resample.
- **Buffer size.** Start at 256 samples for development. Lower for tracking, higher for mixing.

### When this phase is done

You should be able to demo: open the app, see an empty project, drag in an audio file, drop a VST3 on the track, hit play, hear it processed. Save, close, reopen, same state. That's enough to know the foundation holds. Everything else — UI, agent, mixing intelligence — gets built on top of this.

---

## Where this lands you

After Phase 0 (this hello-world) you'll have proven the audio foundation works on your hardware. The next two decisions in priority order:

1. **gRPC schema for the engine API** — the contract that locks down what UI and agent can do. Worth getting right early because every layer depends on it.
2. **First 10 MCP tools** — implemented as actual Python that wraps gRPC calls. This gives Claude its first hooks into the DAW.
3. **Mac-only or cross-platform?** Mac-only halves your scope for v1. Defensible choice given Logic is your reference.

The right next document is probably the proto schema — it forces precision on what's been hand-waved here.
