# CLAUDE.md — DAW Project Guide

This file orients Claude Code to the project on every new session. Read it first, every time.

## What we're building

A modern DAW in the spirit of Logic Pro and Ableton, but agent-native from the ground up. Three differentiators:

1. **A built-in agent** that can mix, add plugins, name tracks, and act as an audio engineer collaborator.
2. **A clean, modern UI** that doesn't carry 20 years of skeuomorphic baggage.
3. **A listening layer** — the DAW continuously analyzes track audio for instrument identification, key/tempo, loudness, and spectral characteristics, and feeds that to the agent.

The DAW must host third-party VST3 / AU plugins. Sample/loop integration with Splice, and AI generation via Suno, are planned for later phases.

## Architecture (read this before writing any code)

Three separate processes communicate over gRPC. The contract is in `proto/daw/v1/`.

```
+------------------+      gRPC (control)      +------------------+
|  Audio Engine    | <----------------------> |   UI Process     |
|  (C++/JUCE)      | <----------------------> |   (Tauri/React)  |
+------------------+                          +------------------+
        ^
        | gRPC (control) + shm ringbuffer (audio)
        v
+------------------+         gRPC             +------------------+
|  Agent Process   | <----------------------> |   Claude (MCP)   |
|  (Python)        |                          +------------------+
+------------------+
```

- **Audio engine** owns the audio graph, plugin instances, and transport. Real-time priority on its audio thread. Source of truth for everything that affects playback. Built on **Tracktion Engine** (which is built on JUCE) — this saves us years of building audio infrastructure from scratch.
- **UI** is stateless. Pulls state from the engine, sends commands, subscribes to events. Tauri shell, React inside.
- **Agent** wraps engine RPCs behind an MCP server that Claude connects to. Also runs ML inference (instrument classification, key/tempo, etc.).

**The cardinal rule:** the audio thread must never be blocked. No allocations, no locks, no file I/O, no network calls in `processBlock` or any audio-thread callback. Tracktion enforces a lot of this for us, but you can still shoot yourself in the foot.

## Project structure (target)

```
daw/
├── CLAUDE.md                # this file
├── README.md                # public-facing intro
├── docs/
│   └── daw-build-planning.md # architecture and roadmap
├── proto/                   # gRPC schema — DONE, this is the contract
├── engine/                  # C++ audio engine, Tracktion-based
│   ├── CMakeLists.txt
│   ├── src/
│   ├── third_party/         # JUCE, Tracktion Engine as submodules
│   └── generated/           # protobuf C++ output
├── agent/                   # Python MCP server + ML
│   ├── pyproject.toml
│   ├── src/
│   └── generated/
├── ui/                      # Tauri + React + TS
│   ├── package.json
│   ├── src/
│   ├── src-tauri/
│   └── src/generated/
└── scripts/                 # dev tooling, build helpers
```

## Where we are right now

✅ **Phase 0: design.** Architecture decided. Proto schema in `proto/daw/v1/` (locked — never break v1).

✅ **Phase 1: foundation validation.** Tracktion's DemoRunner builds and runs on this Mac (record + playback verified, 2026-05-05). Tracktion source is vendored at `engine/third_party/tracktion_engine/` with JUCE pinned to commit `19edd538` under `modules/juce/` (Tracktion master expects that exact JUCE; never use later JUCE develop with it).

🟡 **Phase 2: gRPC layer in the engine — in progress.** First cut working as of 2026-05-05: `clawdaw_engine` boots a Tracktion `Engine` + in-memory `Edit`, runs a sync gRPC server with reflection on `127.0.0.1:50051`, and serves `ListTracks` against the real Edit (returns the 7 tracks Tracktion creates by default). Still to do for the first-10 RPC set: `GetProject`, `GetTrack`, `AddPlugin`, `SetPluginParameter`, `GetPluginParameters`, `SetTrackVolume`, `SetTrackPan`, `RenameTrack`, `Undo`. The first mutation is the architectural pivot — needs JUCE message-thread marshalling design.

⏭️ **Phase 3: minimal UI.** Track list, transport bar, plugin chain view. No mixer skeuomorphism.

⏭️ **Phase 4: agent v1.** Python MCP server exposing the first 10 RPCs as tools.

⏭️ **Phase 5: listening.** Stream audio frames to the agent, classify with YAMNet/PANNs, auto-name tracks on `record_complete`.

Later: full mixer, MIDI editing, automation, Splice/Suno, polished UI.

## Conventions and constraints

**Languages and tools.** C++20 for the engine, Python 3.11+ for the agent, TypeScript 5+ for the UI. Build with CMake (engine), uv or poetry (agent), pnpm (UI). Don't introduce new languages without discussing.

**Proto changes.** Everything lives in `daw.v1`. Any breaking change requires a `daw.v2` package — never modify v1 in a backward-incompatible way once the engine ships. Use `buf breaking` in CI.

**Real-time discipline.** Anything that runs on the audio thread (Tracktion calls into your code from `processBlock` and similar callbacks) must be allocation-free, lock-free, and bounded. If you need to do work that violates these, post it to the message thread.

**Undo is mandatory.** Every mutating RPC returns a `commit_id`. The agent uses this to undo. Never add a mutation that can't be undone.

**Confirmation on destructive ops.** `Delete*` and `Remove*` RPCs require `confirm = true`. Don't bypass this even when "you know" the call is safe.

**Mac-first, but write portable code.** v1 ships Mac-only. Use JUCE / Tracktion abstractions that already exist for cross-platform; don't add Mac-specific APIs without isolating them behind an interface.

## Key external dependencies

- **JUCE** — `https://github.com/juce-framework/JUCE`
- **Tracktion Engine** — `https://github.com/Tracktion/tracktion_engine` (read its license; commercial use needs a license)
- **VST3 SDK** — Steinberg, separate download, has terms
- **gRPC** — for all three layers
- **MCP** (Python) — `mcp` package on PyPI, for the agent's exposure to Claude
- **PyTorch / ONNX Runtime** — for the agent's ML inference (YAMNet / PANNs starting point)
- **Tauri 2** — for the UI shell

## What to do in your next session

1. Read `docs/session-log.md` from the top — newest entries first. It's the authoritative state of where we left off, what worked, and what's queued.
2. Skim `proto/daw/v1/engine.proto` for the next RPC you're about to implement so the contract is fresh.
3. If `engine/third_party/` or `engine/generated/` are missing on disk (they're gitignored), restore them before building:
   - Tracktion + JUCE vendoring steps live in the 2026-05-05 entries of `docs/session-log.md` (curl tarballs into `engine/third_party/_downloads/`, extract Tracktion to `engine/third_party/tracktion_engine/`, extract JUCE-pinned at sha `19edd538` into `engine/third_party/tracktion_engine/modules/juce/`).
   - Generated proto stubs: `cd proto && buf generate`.
4. Sanity-check the engine still builds: `cd engine && cmake -B build && cmake --build build --target clawdaw_engine -j 8`. Run it: `./engine/build/clawdaw_engine` should print `clawdaw_engine listening on 127.0.0.1:50051 (tracks in edit: 7)`.
5. Then the actual work for the session — usually the next RPC from the Phase 2 first-10 list. The first mutation forces the JUCE message-thread design choice; don't decide it tired.

Don't add features beyond the next RPC unless you can articulate why. The proto contract already implies more than we have time for.

## When in doubt

- **Architecture questions:** see `docs/daw-build-planning.md`.
- **API questions:** see `proto/README.md` and the `.proto` files themselves.
- **"Should I add this dependency?":** ask Sammy first.
- **"Should I optimize this?":** no, not yet. Make it work, then make it right, then make it fast.
