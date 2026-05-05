# DAW Engine API — Protobuf Schema

This is the contract between the three processes of the DAW: the **audio engine** (C++), the **UI** (TypeScript), and the **agent** (Python). All three speak gRPC, and these `.proto` files define what they can say.

## File layout

```
proto/
├── buf.yaml              # buf lint/breaking-change config
├── buf.gen.yaml          # code generation config (C++, Python, TS)
├── README.md             # this file
└── daw/v1/
    ├── common.proto      # IDs, enums, primitive types
    ├── project.proto     # Project, Track, Region, MidiNote
    ├── plugin.proto      # PluginInfo, PluginInstance, parameters
    ├── transport.proto   # TransportState
    ├── mixer.proto       # Mixer, Send, AutomationLane
    ├── analysis.proto    # TrackAnalysis, AudioFrame (the listening layer)
    ├── events.proto      # Event types for SubscribeEvents
    └── engine.proto      # The Engine service + all request/response messages
```

## Conventions

**Package versioning.** Everything lives in `daw.v1`. When the schema needs breaking changes, we'll create `daw.v2` alongside and migrate consumers. Don't break v1.

**IDs are plain strings.** UUIDs in practice. We don't wrap them in message types because the verbosity isn't worth the marginal type safety in Python and TypeScript.

**Time is dual-format.** `TimePosition` carries both seconds and bars. Callers pass whichever is meaningful; the engine populates both on read. If both are set on a write, seconds wins.

**Mutating RPCs return `MutationResult`.** Every operation that changes state returns a `commit_id`. Pass it back to `Undo` to revert that specific change. This is non-negotiable: agents need to feel safe trying things.

**Confirmation flags on destructive ops.** `DeleteTrack`, `DeleteRegion`, `RemovePlugin` all require `confirm = true`. The UI sets it after a confirmation dialog; the agent sets it after Claude has explicit user approval.

**Field numbers leave gaps.** In `Event.payload`, fields are spaced (5, 10, 20, 30, ...) so we can add related events without renumbering.

## Compilation

We use [buf](https://buf.build) for linting and codegen. Install it first:

```bash
brew install bufbuild/buf/buf      # macOS
# or: https://buf.build/docs/installation
```

Lint:

```bash
cd proto
buf lint
```

Check for breaking changes against the main branch:

```bash
buf breaking --against '.git#branch=main'
```

Generate code for all three targets:

```bash
buf generate
```

This drops generated code into:

- `../engine/generated/` — C++ headers and gRPC stubs
- `../agent/generated/` — Python modules
- `../ui/src/generated/` — TypeScript modules (using bufbuild/es)

## The big picture: how the three layers use this

**Engine (C++).** Implements the `Engine` service. Owns the audio graph, plugin instances, transport. Streams events out to subscribers. Real-time priority on its audio thread; everything in this RPC layer runs on a separate message thread.

**UI (TypeScript).** Calls read RPCs to populate views, calls mutating RPCs in response to user actions, subscribes to `SubscribeEvents` to stay in sync with state changes from other clients (notably the agent).

**Agent (Python).** Wraps these gRPC calls behind an MCP server that Claude connects to. The agent's MCP tool surface is roughly a curated, simplified subset of the gRPC surface — the proto API is granular by design, but the MCP tools should be at the level of intent ("EQ this track to fix muddiness") rather than mechanism ("set parameter 0x27 to 0.43").

## What's NOT in v1

Deliberate omissions, to be added once core works:

- Multi-user / collaboration RPCs
- Cloud project sync
- Hardware controller mapping (Mackie, MIDI surfaces)
- ARA (Audio Random Access) plugin support
- Video / film scoring features
- Bounce / export (will need its own proto file when added)
- Sidechain routing (the `Send` message will gain fields when added)

## Questions that need answering before serious implementation

1. **Streaming back-pressure.** `StreamTrackAudio` could overwhelm slow consumers. Should we use gRPC flow control, or define an explicit ack-based protocol? Probably the former with a generous buffer.
2. **Consistency model for `SubscribeEvents`.** If the agent subscribes mid-session, should it receive a snapshot first? Probably yes — consider a `SubscribeEventsWithSnapshot` variant.
3. **Plugin scanning.** `RescanPlugins` is exposed as a single RPC, but real scans take minutes and can crash on bad plugins. Should it be streaming? Should it run in a sandboxed sub-process? Probably yes to both.
4. **Project schema versioning on disk.** The on-disk project file is separate from this RPC schema; it'll need its own version field and migrator.
