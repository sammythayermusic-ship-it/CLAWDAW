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

✅ **Phase 0: design.** The architecture is decided. Proto schema is drafted at `proto/daw/v1/`.

⏭️ **Phase 1: foundation validation.** Get Tracktion Engine running on the dev machine, play a file through a VST3, save and reload an Edit. This is "hello world" — see Part 3 of `docs/daw-build-planning.md` for the validation checklist. **Don't skip this. Two evenings, max.** The whole project depends on confirming the foundation works on this hardware.

⏭️ **Phase 2: implement the gRPC layer in the engine.** Wire the Tracktion Engine actions to the proto-defined RPCs. Start with the read-side and basic mutations (the first 10 from the planning doc): `GetProject`, `GetTrack`, `ListTracks`, `AddPlugin`, `SetPluginParameter`, `GetPluginParameters`, `SetTrackVolume`, `SetTrackPan`, `RenameTrack`, `Undo`.

⏭️ **Phase 3: minimal UI.** A track list, a transport bar, a plugin chain view. No mixer skeuomorphism. Just enough to interact.

⏭️ **Phase 4: agent v1.** Python MCP server that exposes the first 10 tools. Demo: Claude lists tracks, adds an EQ, tweaks parameters.

⏭️ **Phase 5: listening.** Stream audio frames out to the agent, run YAMNet or PANNs on them, return classification. Auto-name tracks on `record_complete` events.

Later: full mixer, MIDI editing, automation, Splice/Suno integration, polished UI.

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

## What to do in your first session

1. Read `docs/daw-build-planning.md` end to end.
2. Read `proto/README.md` and skim the proto files to internalize the data model.
3. Set up the `engine/` directory: clone JUCE and Tracktion Engine as submodules under `engine/third_party/`, write a `CMakeLists.txt` that builds against them.
4. Build one of Tracktion's example projects unmodified, confirm it runs on this Mac.
5. Then start the Phase 1 hello-world: a small program that plays an audio file through a VST3 and saves/reloads the Edit.

Don't write any of the gRPC layer until the audio foundation works. The temptation to start scaffolding everything at once is strong; resist it. A working `processBlock` chain is the only thing that proves the architecture is viable on this hardware.

## When in doubt

- **Architecture questions:** see `docs/daw-build-planning.md`.
- **API questions:** see `proto/README.md` and the `.proto` files themselves.
- **"Should I add this dependency?":** ask Sammy first.
- **"Should I optimize this?":** no, not yet. Make it work, then make it right, then make it fast.
