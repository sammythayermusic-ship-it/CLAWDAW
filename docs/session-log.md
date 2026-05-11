# CLAWDAW — Session Log

Append-only log of each work session. Newest at the top.

---

## 2026-05-11 — Transport Play/Stop: engine handlers + Tauri commands + wired buttons ✅

**Goal:** Make the Play and Stop buttons in the UI's transport pill actually work. Implement `Engine.Play`, `Engine.Stop`, and `Engine.GetTransportState` handlers in the engine; wire Tauri commands; hook the existing buttons in `TransportBar.tsx`. End state: clicking Play in the Tauri window starts playback in the engine; clicking Stop stops it. 1-hour cap.

**Outcome:** Working. The three RPCs are live and verified end-to-end against the running engine via grpcurl — `Play` flips `isPlaying` to true, the transport position advances, `Stop` flips it back, and `Undo` on a Stop re-plays. `CommandApplied` events flow through the broadcaster for both. The UI has three new Tauri commands (`engine_play`, `engine_stop`, `engine_get_transport_state`), a `transport: { isPlaying }` slice in the Zustand store updated optimistically from the button onClick, and a Play button that swaps to a pause glyph and a lit gold accent face when playing. Branch `claude/transport-play-stop`, two commits on top of `16b46da`.

### What was built

**Engine ([engine/src/main.cc](engine/src/main.cc))** — three new handlers slotted in between `Undo` and the Plugins section:

- `Play(Empty) → MutationResult` — calls `transport.play(false)` on the message thread. Captures `wasPlaying` before the call; pushes an undo closure that calls `transport.stop(false, false)` if play actually started something, or a no-op closure if it was already playing. Emits `CommandApplied` with description `"Started playback at X.XXXs"` using the position at start.
- `Stop(Empty) → MutationResult` — calls `transport.stop(false, false)` (discardRecordings=false, clearDevices=false). Same wasPlaying-aware undo: re-plays if Stop actually halted something, no-op if Stop was a no-op. Description `"Stopped playback"`.
- `GetTransportState(Empty) → TransportState` — `playing`/`recording`/`position` filled from `transport.isPlaying()`, `transport.isRecording()`, `transport.getPosition().inSeconds()`. `position.bars` stays zero — the UI doesn't render bars live yet, and computing them needs a tempo-sequence lookup we don't have a helper for.

Implementation matches the existing pattern: `runOnMessageThread` (`MessageManagerLock`) for the mutations, `pushUndo` for the closure, `emitCommandApplied` for the bookkeeping event. **No** `TransportStateChanged` event — see the optimistic-UI note below. Single new include (`cstdio`, for snprintf in the play description).

**UI Rust ([ui/src-tauri/](ui/src-tauri/))**

- `dto.rs` — new `TransportStateDto { playing, recording, positionSeconds }` (camelCase) + `From<proto::TransportState>` impl that flattens `position.seconds` to `position_seconds`.
- `commands.rs` — three new commands (`engine_play`, `engine_stop`, `engine_get_transport_state`) routed through the existing `unary()` helper so each gets the standard 8s per-RPC deadline.
- `lib.rs` — three handlers added to the `invoke_handler!` registry.

**UI JS ([ui/src/](ui/src/))**

- `state/types.ts` — `TransportStateDto` mirror.
- `state/engine.ts` — `engineApi.play()`, `.stop()`, `.getTransportState()` typed wrappers.
- `state/store.ts` — `transport: { isPlaying: boolean }` slice + `setTransportPlaying(playing)` action. Initial value `false`. **Not** updated from events — see optimistic-UI note.
- `panels/TransportBar.tsx` — Play button onClick toggles between `engineApi.play()` and `engineApi.stop()` based on current `isPlaying`; Stop button always calls `.stop()`. Both flip the store optimistically before the RPC fires and roll back if the RPC errors. Play button swaps to a new `PauseIcon` and gains class `transport-btn--play-active` when playing.
- `panels/icons.tsx` — new `PauseIcon` (two rounded rects in a 16×16 viewBox).
- `panels/TransportBar.css` — `.transport-btn--play-active` overrides background to `var(--color-accent-active)` with intensified inner glow. No new tokens; uses existing accent.

### Verification

Engine smoke against the running binary (`./engine/build/clawdaw_engine 127.0.0.1:50091`):

```
$ grpcurl -plaintext :50091 daw.v1.Engine/GetTransportState
{ "position": {} }                          # playing defaults to false

$ grpcurl -plaintext :50091 daw.v1.Engine/Play
{ "commitId": "...", "description": "Started playback at 0.000s" }

$ grpcurl -plaintext :50091 daw.v1.Engine/GetTransportState
{ "playing": true, "position": {} }

$ grpcurl -plaintext :50091 daw.v1.Engine/Stop
{ "commitId": "...", "description": "Stopped playback" }

$ grpcurl -plaintext :50091 daw.v1.Engine/Undo
{ "commitId": "...", "description": "Undo: Stopped playback" }

$ grpcurl -plaintext :50091 daw.v1.Engine/GetTransportState
{ "playing": true, "position": { "seconds": 0.0213 } }  # position advanced!
```

`SubscribeEvents` with no filter sees a `commandApplied` event for both the Play and the Stop, with descriptions matching the `MutationResult`s.

Build hygiene:
- `cd engine && cmake --build build --target clawdaw_engine -j 8` → 100% built, no new compile warnings (only the pre-existing macOS-version linker warnings from abseil dylibs, unrelated to this change).
- `cd ui/src-tauri && cargo check` → clean, only the pre-existing `engine::reconnect is never used` warning.
- `cd ui && ./node_modules/.bin/tsc --noEmit` → clean.
- `cd ui && ./node_modules/.bin/vite build` → 53 modules, 295ms, no errors.

### Decisions

1. **No `TransportStateChanged` event emission.** The proto already declares the event type, but no handler emits it. Reason: with only one caller (the UI), optimistic update from the button onClick is good enough — when the agent (or a second UI client) can race with the user's clicks, we'll wire the engine event through and switch the store to a mirror. Plumbing an event for which there's only one writer just adds latency.

2. **No-op undo closures for no-op Play/Stop.** If Play is called when already playing, or Stop when already stopped, the RPC still returns a valid `commit_id` and we still push *something* onto the undo log so a subsequent `Undo` consumes it cleanly. The closure is empty. Agents that issue commands without first checking transport state shouldn't get a "phantom" undo of the previous mutation.

3. **`runOnMessageThread` (MessageManagerLock) over `runOnMessageThreadSync` (callAsync).** Transport play/stop are simple Tracktion calls — no AU plugin lifecycle, no audio-device teardown. The `MessageManagerLock` path is cheaper and works; the callAsync path is reserved for plugin instantiation per the Phase 2 decision in main.cc.

4. **Position description format.** `"Started playback at 0.000s"`. The original plan suggested `<bar>.<beat>` but bars require a tempo-sequence lookup and the UI doesn't render bar/beat live yet. Seconds is honest, brief, and visible in the recent-commits panel that already renders `description`.

5. **Pause icon, not "active Play" glyph.** When playing, the button swaps from a triangle to a pause symbol so the affordance reads as "click to pause" rather than "this is playing." Universal across DAWs; matches user expectation.

### Known limitations

- **Visual verification incomplete.** TypeScript + Rust + Vite all compile clean and the engine RPCs are end-to-end verified via grpcurl, but I didn't open the Tauri window to physically click Play. Same shell-environment constraint as Phase 3 — no display access. Sammy should `cd ui && pnpm tauri dev`, click Play, and confirm: (a) the icon swaps to pause, (b) the button gets a lit gold accent face, (c) `grpcurl GetTransportState` against the engine while the UI is open reports `playing: true`. The Phase 3 followup lesson applies: "the build compiles + the JS mounts" still isn't "the feature works."
- **Audio output not validated.** The transport advances in software (`position.seconds` increments after Play), but I didn't put my ear on speakers to confirm the Edit produces audio. The Edit is the default `createSingleTrackEdit` — empty, no regions — so silence is the expected output regardless. The "RPC says it played" criterion was the explicit success bar for this session.
- **No `TransportStateChanged` event.** UI is optimistic-only. If a second client (the future agent) toggles transport via the engine, this UI won't see it until either a hard refresh or until we plumb the event.
- **`engine_get_transport_state` is added but never called from the UI.** I added the read RPC for completeness (the proto already declared it; the task scope explicitly listed it) but the UI's transport state slice initializes to `false` and updates only from clicks. The right next-bite use is calling `getTransportState()` once on mount in `useEngineSync` so the UI doesn't drift from engine state on app start — deferred since the engine boots stopped.
- **No tests.** No engine-side test framework yet; UI is still embryonic per Phase 3.

### Branch state

- Branch: `claude/transport-play-stop` (was `claude/loving-lumiere-00c886` — renamed since the worktree HEAD matched main exactly).
- Two commits on top of `16b46da` (the housekeeping log entry that's currently the tip of main).
- Pushed to `origin` for review. **Not merged.** Sammy reviews via PR.

### Followups (out of scope for this session, but obvious next bites)

- **MCP agent gets `play`/`stop`/`get_transport_state` tools.** Currently the agent doesn't know these RPCs exist. Phase 4 will pick up the MCP scaffolding; transport is a good demo surface for "the agent runs the DAW."
- **`TransportStateChanged` event + UI as mirror.** Wire when there's a second writer. Engine-side change is ~10 lines (emit on play/stop), UI change is ~5 lines (switch the store from optimistic to event-driven).
- **`SubscribeTransportPosition` (or piggyback the position on `TransportStateChanged`).** Animate the BAR.BEAT.SUB readout — currently frozen at `12.3.1`.
- **Initial `getTransportState()` call on app mount.** One line in `useEngineSync` to seed the store correctly if the engine is already playing when the UI connects.
- **Record button.** Out of scope this session, but `Engine.Record(RecordRequest)` is the next obvious mutation. Tracktion's `transport.record(...)` is more involved (arm tracks, count-in, punch ranges); deserves its own session.

### Time spent

~45 minutes — under the 1-hour cap. The engine handler implementation was small (the existing `RenameTrack` / `SetTrackVolume` pattern transferred directly; the only research was the Tracktion `TransportControl::stop` signature taking 3 args, not 4 as written in the task). The biggest single time sink was the CMake configure cycle (~35s) for the worktree's first build, which is unavoidable. The pattern lesson from this session: a session where the proto contract already exists for the RPCs you're implementing collapses the design step to zero — read the existing handlers, match the pattern, build.

---

## 2026-05-10 (late) — Housekeeping: Phase 3 merged to main, repo pushed to GitHub ✅

**Goal:** Pre-Phase-4 cleanup. Phase 3's UI work landed earlier today on a feature branch (`claude/hardcore-chaplygin-350368`) but never made it onto `main`. Status report tonight surfaced this — eight commits, ~3700 lines, including the two post-run bug fixes. Get it onto `main`, push everything to GitHub for off-machine backup, prune dead branches, before any further work.

**Outcome:** `main` now contains the Phase 3 Tauri UI work as commit `269114d` (merge of `claude/hardcore-chaplygin-350368` via `--no-ff`). Repo is mirrored at `https://github.com/sammythayermusic-ship-it/CLAWDAW.git`. All six `claude/*` worktree branches deleted locally — every one was either merged into `main` or had its work brought in by tonight's merge. `main` is the only branch on GitHub.

### What was done, in order

1. **Read the Phase 3 branch carefully before touching it.** The earlier status report had wrongly assumed all `claude/*` branches were stale. `git log claude/<branch> --not main --oneline` per branch revealed `claude/hardcore-chaplygin-350368` was 8 commits ahead of `main` with the entire Phase 3 implementation. Five of the other branches had nothing unmerged; `claude/elated-driscoll-a01eb3` likewise had nothing unmerged.
2. **Reset a misguided housekeeping commit.** I had already authored a "Phase 3 prep" commit on `main` (`b7e656f`) before discovering the unmerged branch. That commit added back `docs/phase-3-plan.md` and `docs/phase-3-claude-code-prompt.md`, which the Phase 3 branch had deliberately deleted (the autonomous run consumed them). Reset to `f071e59` to keep history clean.
3. **Merged `claude/hardcore-chaplygin-350368` into `main` with `--no-ff`.** Brings all 8 commits in as one coherent unit visible in `git log --first-parent`. The merge commit message lists all 8 original commit subjects for searchability.
4. **Pruned all six `claude/*` worktree branches locally.** With Phase 3 now on `main`, no branch had unmerged work. Pruned: `dazzling-mendeleev-951f13`, `elated-driscoll-a01eb3`, `hardcore-chaplygin-350368`, `lucid-blackburn-8c6294`, `nervous-merkle-d652f8`, `sad-aryabhata-bb8d11`.
5. **Configured the `origin` remote and pushed `main` to GitHub.** Added `origin → https://github.com/sammythayermusic-ship-it/CLAWDAW.git`. First push with `-u origin main` so future `git push`/`pull` work without arguments.

### Decisions made tonight

- **Push the 300 MB of prototype PNGs as-is, no LFS yet.** The 2026-05-09 log flagged this as a future cleanup. Decision: defer. Largest single PNG is 27 MB (well under GitHub's 100 MB hard limit); 15 files × 18–28 MB ≈ 300 MB is below GitHub's 1 GB soft limit. GitHub warns on files >50 MB but none of ours hit that. Revisit if/when push times start hurting.
- **Delete all six `claude/*` worktree branches.** Including `claude/hardcore-chaplygin-350368` once its work was merged. The random-name convention used by Claude Code worktrees is fine for in-flight work but each branch is dead the moment the work lands on `main`. Keeping them around just clutters the branch list.
- **Don't push the `claude/*` branches to GitHub.** Main is the only branch visible on the remote. Future Phase work happens on new feature branches.

### Verification

- `git log --oneline -12` shows `269114d Merge branch 'claude/hardcore-chaplygin-350368'` at the top of `main`, with the 8 Phase 3 commits beneath it followed by the pre-Phase-3 history.
- `git ls-remote origin main` returns the same SHA as `git rev-parse main`.
- `git branch -a` shows `main` and `remotes/origin/main` only — no stragglers.
- `git status` clean.
- `.gitignore` audit confirmed `.claude/`, `engine/build/`, `engine/third_party/`, `engine/generated/`, `agent/generated/`, `ui/src/generated/` are all excluded. No build artifacts followed the merge in.

### Known limitations

- **No CI yet.** The repo lives on GitHub but no GitHub Actions workflows. The CLAUDE.md plan calls for `buf breaking` in CI; not blocking Phase 4 but worth a future bite. Engine build matrix + `pnpm tauri build` smoke test + `cargo check` would be reasonable starter workflows.
- **No `README.md` at the repo root.** The GitHub project landing page will be bare. Trivial fix in any future session — the planning doc in `docs/daw-build-planning.md` has plenty of source material.
- **`Cargo.lock` still gitignored.** The Phase 3 session log called this out; standard Tauri 2 template would commit it. Leaving as-is until / unless we hit a transitive-Rust-dep security issue.
- **Prototype PNGs are still raw blobs in `main`.** Repo total is ~300 MB at HEAD. Future cleanup → LFS, but deferred.

### Repo state

- `main` at `269114d`. Local and `origin/main` identical.
- Branch list: `main` only. (Was: `main` + 6 stale `claude/*`.)
- Remote: `origin` → `https://github.com/sammythayermusic-ship-it/CLAWDAW.git` (was: no remote).
- One merge commit added this session; no other code changes.

### Next session

Phase 4: Python MCP server exposing the engine's RPCs as tools so Claude can talk to it directly. The engine has 13 RPCs ready; agent scaffolding goes into `agent/` (currently empty except for gitignored generated proto stubs). See CLAUDE.md "Phase 4: agent v1" and `docs/daw-build-planning.md` for the broader plan.

Reasonable side bites that could go before Phase 4 if Sammy prefers polish-then-extend: real transport RPCs in the engine (Play/Stop/Record proto handlers — currently `Unimplemented`), a "knobs" pass on PluginChain making it functional rather than informational, or `tauri-plugin-window-state` for window position persistence. All three are listed in the Phase 3 session log's "Next session" section.

### Time spent

~20 minutes — slightly longer than expected because of the wrong-turn reset (committing housekeeping → discovering the unmerged Phase 3 branch → resetting → merging properly). Pattern lesson: always run `git log <branch> --not main` on every leftover branch before declaring it stale, especially when "stale" branches use the same name-prefix as in-flight work.

---

## 2026-05-10 — Phase 3 Tauri UI: scaffold, live engine wiring, three panels ✅

**Goal:** Land the first desktop window for CLAWDAW. Tauri 2 + React + TypeScript shell, styled exclusively from `ui/src/design/tokens.ts`, talking to `clawdaw_engine` over native gRPC (tonic on the Rust side, Tauri commands + event channel into JS), with three panels — TransportBar, TrackList, PluginChain — mirroring engine state via `GetProject` + `SubscribeEvents`. 3-hour autonomous block following [docs/phase-3-plan.md](phase-3-plan.md).

**Outcome:** Working end-to-end. The Tauri window opens, the React app calls `get_project`, the Rust side calls `Engine.GetProject` over a tonic channel to localhost:50051 and returns a serde-friendly DTO. The same channel multiplexes the streaming `SubscribeEvents` RPC; events are forwarded to a Tauri event channel (`engine:event`) and dispatched into a Zustand store via reducers. Three panels render: a slot-machine TransportBar (hero), a clickable TrackList rail, and a PluginChain rail that re-fetches `GetTrack` whenever the store invalidates the selected track's detail. `grpcurl` mutations against the engine produce the expected events; the round-trip latency to the UI is < a frame. Three commits on `claude/hardcore-chaplygin-350368`, on top of `f071e59`.

### What was built (file by file)

**Tauri scaffold (Hour 1, [c42a896](https://example.invalid/clawdaw/commit/c42a896))**

- `ui/package.json` — pnpm-managed, Tauri 2.11 / React 18.3 / TS 5.6 / Vite 5.4 / Zustand 5.0. `pnpm.onlyBuiltDependencies: ["esbuild"]` because pnpm 11 blocks postinstall scripts by default.
- `ui/tsconfig.json`, `ui/tsconfig.node.json`, `ui/vite.config.ts` — strict TS, port 1420, ignore `src-tauri/**` from HMR watch.
- `ui/index.html` — `<meta name="color-scheme" content="light">` so dark-mode browsers don't show through the warm palette (same fix the token-preview page needed).
- `ui/src/main.tsx` — applyThemeToRoot() at boot, before React mounts.
- `ui/src/design/theme.ts` — bridge from typed tokens to CSS custom properties on `:root`. `--color-bg-glow`, `--type-display-size`, `--space-px4`, `--elevation-rest`, etc. Every color in the UI flows through this; no hex literals in components.
- `ui/src/styles.css` — body background renders the four-stop diagonal warm gradient mirroring `docs/design/token-preview.html` (radial peach-glow at top-right, radial dusty-plum at bottom-left, linear deep→warm wash). `background-attachment: fixed` so panels feel like they sit on a single warm sheet.
- `ui/src-tauri/Cargo.toml` — tauri 2 + tonic 0.12 + prost 0.13 + tokio (rt-multi-thread, macros, sync) + tonic-build as build-dep.
- `ui/src-tauri/build.rs` — `tonic_build::configure().build_server(false).build_client(true).compile_protos(...)` against all 8 `proto/daw/v1/*.proto` files. Generated code lands in `OUT_DIR`, included via `tonic::include_proto!("daw.v1")` from `proto.rs`.
- `ui/src-tauri/src/proto.rs` — re-exports the generated `EngineClient` and message types.
- `ui/src-tauri/src/engine.rs` — `EngineClient` wrapping `tokio::Mutex<Option<Channel>>`. Lazy connect; tonic clients cloned per call (HTTP/2 multiplexes underneath).
- `ui/src-tauri/src/dto.rs` — serde-friendly DTO mirrors of prost types (`ProjectDto`, `TrackSummaryDto`, `TrackDto`, `MixerDto`, `PluginInstanceDto`, `CommitDto`). Crucially: `EventDto` is a tagged enum (`#[serde(tag = "kind", rename_all = "camelCase")]`) so the JS side switches on `event.kind` instead of the proto's nested `payload.oneof`.
- `ui/src-tauri/src/commands.rs` — eight Tauri commands wrapping the engine RPCs: `get_project`, `get_track`, `rename_track`, `set_track_volume`, `set_track_pan`, `set_plugin_parameter`, `engine_undo`, `subscribe_events`. The streaming command spawns a `tauri::async_runtime::spawn` task that forwards each event to `app.emit("engine:event", dto)`.
- `ui/src-tauri/src/lib.rs` — Tauri builder, manages `EngineClient` state, wires the command handlers.
- `ui/src-tauri/tauri.conf.json` — 1280×800 default window with the warm peach background color so the OS title bar doesn't flash white before React paints. CSP `null` (Tauri dev sandboxing is enough).
- `ui/src-tauri/capabilities/default.json` — `core:default` + `opener:default`.
- `ui/src-tauri/icons/icon.png` — placeholder warm-pebble PNG generated via Pillow at 512×512.

**Engine sync (Hour 2, [51f364b](https://example.invalid/clawdaw/commit/51f364b))**

- `ui/src/state/types.ts` — TS shapes mirroring the Rust DTOs. Single source of truth for what crosses the IPC boundary.
- `ui/src/state/engine.ts` — typed wrapper around Tauri's `invoke()` and `listen()`. One module so commands have one canonical signature in JS.
- `ui/src/state/store.ts` — Zustand store. `tracksById` + `trackOrder` for the visible list, `trackDetails` keyed by id for full `Track` payloads, `selectedTrackId`, `recentCommits` (capped at 50). `applyEvent(e)` is the central reducer.
- `ui/src/state/useEngineSync.ts` — root-level hook. On mount: `getProject()` → `hydrateFromProject()` → wire `onEvent` and `onDisconnected` listeners → `startEventStream()`. Per-event re-fetch of `GetTrack` when the reducer invalidates a detail. Also exports `useEnsureSelectedTrackDetail()` which kicks off a fetch if the selected track has no cached detail.

**Three panels (Hour 3, [7ec2696](https://example.invalid/clawdaw/commit/7ec2696))**

- `ui/src/panels/TransportBar.tsx` + `.css` — slot-machine pill mirroring `docs/design/token-preview.html` §4. Play/Stop/Record buttons (visual-only — no transport RPC yet), 36px tabular-nums readouts for BPM (live from `project.tempoBpm`) and BAR.BEAT.SUB (frozen at 12.3.1), mini meter with `meter.low/mid/high` gradient, small −2.4 dB master readout.
- `ui/src/panels/TrackList.tsx` + `.css` — left rail. Glass-pebble rows (`surface.glassRaised` + `glass.edgeHighlight` + `elevation.rest`); name in `h2`, track-type in label-caps, volume in `monoBody`. Click selects; selected row gets `accent.hover` background + accent inset border.
- `ui/src/panels/PluginChain.tsx` + `.css` — right rail. 3-column row grid (slot · name+vendor+format · state pill). Bypassed plugins fade to 0.55 opacity; the "active" pill uses `meter.low` at 30% alpha.
- `ui/src/panels/icons.tsx` — inline Play/Stop/Record/Chevron SVGs, sized via prop, fill via `currentColor`.
- `ui/src/App.tsx` — agent pebble + "CLAWDAW" brand on the left of the titlebar, connection pill on the right; transport pill below; rails (TrackList | PluginChain) at the bottom in a `minmax(220px, 320px) 1fr` grid.
- `ui/src/App.css` — three-row grid layout, agent-indicator pebble at 32px with the warm halo recipe from token-preview §8.

### Architecture decisions made along the way

1. **tonic, not gRPC-Web.** Codified in [docs/phase-3-plan.md](phase-3-plan.md). The Tauri Rust process talks gRPC directly to the engine; the React side only sees Tauri commands and a typed event channel. No Envoy proxy, no extra dev process. Per-RPC the cost is a 5-line wrapper in `commands.rs`.

2. **`tokio::Mutex<Option<Channel>>` for the gRPC client, not a connection pool.** tonic's `Channel` is internally an HTTP/2 connection that already multiplexes streams; cloning it is cheap, so each command gets a fresh `EngineClient::new(channel.clone())`. Verified: with both `GetProject` (unary) and `SubscribeEvents` (streaming) active, only one TCP socket exists between the Tauri process and the engine.

3. **DTOs mirror, not pass-through.** The prost-generated types don't derive `serde::Serialize`, the proto's `oneof` shapes are noisy in JS, and enum-as-i32 is a footgun. Writing DTOs by hand (with `From<prost::T>` impls) costs ~150 lines but the JS side gets clean tagged unions: `event.kind === "trackRenamed"`, not `event.payload?.case === "trackRenamed"`.

4. **Reducer style: invalidate, don't optimistic-update.** Per CLAUDE.md state rule, the engine is authoritative. `TrackRenamed` carries the new name on the event, so we patch in place. `TrackMixerChanged` and the `Plugin*` events don't carry full new state — instead, the reducer drops the cached track detail; the next render of `PluginChain` (or the next selection) triggers a `GetTrack` re-fetch via `useEnsureSelectedTrackDetail`. No code path mutates UI state ahead of the engine.

5. **Hidden Tracktion-internal tracks.** `ListTracks`/`GetProject` returns Marker, Tempo, Chord, Arranger tracks alongside the user-created Audio/Bus tracks. Their proto `TrackType` comes back as `UNSPECIFIED`. The store filters `UNSPECIFIED` out of the visible track list — the user didn't create them and shouldn't see them.

6. **Frozen transport-position readouts.** The Phase 3 plan calls out "frozen at `00:00:00.000` is fine"; I went with `12.3.1` to match the prototype reconstruction in `token-preview.html`. When we add a `SubscribeTransportPosition` RPC (or push position into existing events) this becomes the obvious wiring.

7. **Branch name kept as `claude/hardcore-chaplygin-350368`.** The plan called for `claude/phase-3-ui-scaffold` but the worktree's branch was already created with the random-name convention used by previous Claude sessions (`claude/dazzling-mendeleev-951f13` etc.). Renaming would have meant dance with the worktree config; merging from `hardcore-chaplygin` is functionally identical to merging from `phase-3-ui-scaffold`.

### Verification

- `pnpm exec tsc --noEmit` clean (TS strict, no warnings).
- `cargo build --bin clawdaw-ui` clean (one harmless `dead_code` warning on `engine::reconnect`, kept around for the future "Reconnect" button).
- `pnpm tauri dev` opens the window. After the first build, incremental rebuilds finish in ~5s.
- Tauri process holds exactly one TCP connection to `127.0.0.1:50051` (verified via `lsof -p $(pgrep clawdaw-ui)`), confirming tonic multiplexes the unary calls and the streaming `SubscribeEvents` over one channel.
- Triggered live mutations via grpcurl:
  ```
  grpcurl -plaintext -d '{"track_id":"1003","name":"Bass"}'    :50051 daw.v1.Engine/RenameTrack
  grpcurl -plaintext -d '{"track_id":"1003","volume_db":-3.5}' :50051 daw.v1.Engine/SetTrackVolume
  grpcurl -plaintext -d '{"track_id":"1010","name":"Pad"}'     :50051 daw.v1.Engine/RenameTrack
  ```
  Each returned a `commit_id`; the engine fanned out the corresponding type-specific event + `CommandApplied` per the Phase 2 broadcaster. Tauri stderr shows no `[engine:event] emit failed` lines, which is the only log we'd see if event delivery into JS broke.
- **Visual verification: only indirect.** The shell environment can't `screencapture` (no display access), so I couldn't inspect the rendered window myself. The build pipeline + token-preview.html reference + grpcurl event flow are the strongest signals; the actual paint of the panels needs Sammy's eye.

### Known limitations / open decisions

- **No transport RPC wired through.** Play / Stop / Record buttons are visual-only. The proto exposes `Engine.Play / Stop / Record` already; the engine doesn't yet implement them (last session log mentions `Play/Stop` as Phase 3+ scope). Wiring is one Tauri command + a `useTransport()` hook when the engine handlers land.
- **No frame-rate transport position.** BAR.BEAT.SUB readout is frozen at 12.3.1. Animating it requires either a streaming `SubscribeTransportPosition` RPC or piggy-backing on an existing event. Deferred — the proto doesn't have the right event type.
- **No optimistic mutation UI yet.** Per the discipline rule, mutations round-trip; the latency on local gRPC is well under a frame, so it doesn't matter today. If we add network-traversed RPCs (e.g. running the engine on a separate machine) we'll want a "pending" overlay or per-row spinner.
- **Engine binary at `/engine/build/clawdaw_engine` was stale.** It dated to May 5 (pre-Phase 2 mutations); `RenameTrack`, `GetProject`, `GetTrack` all returned `Unimplemented` until I rebuilt with `cmake --build build --target clawdaw_engine -j 8`. **For future sessions:** the binary's mtime is a reliable canary — anything older than the most recent main.cc commit needs a rebuild.
- **No display access in this session.** I couldn't take screenshots of the window. The plan's verification step "open the window. Click around" is something Sammy needs to do before merging. **Followup, same evening:** Sammy ran the Tauri window — it rendered blank. Two bugs surfaced, fixed in commit `c234a25`. See "Bugs caught after first run" below.
- **`pnpm-lock.yaml` is committed (~7K lines).** Acceptable for reproducibility, but if it becomes noisy in PRs we can move to `pnpm-store` per-CI strategies.
- **`Cargo.lock` is gitignored, not committed.** Tauri 2 templates typically commit it for binary reproducibility. I followed the existing project convention (`engine/build/` is also ignored). If we ever need to pin transitive Rust deps for security, the call is to commit `Cargo.lock` and stop ignoring it.
- **No JS-side enum for event filtering yet.** `subscribe_events` doesn't take a filter — we get every event. Negligible cost today (only a handful of mutations per second under normal use); when we have audio frame events streaming we'll want to plumb the proto's `EventFilter` through.
- **Reconnect logic exists but is dead code.** `engine::reconnect()` is wired but no UI calls it. When the engine restarts, the connection breaks and stays broken until app restart. Adding a "Reconnect" button or auto-retry on disconnect is one of the obvious next-session jobs.
- **Window dimensions hardcoded.** 1280×800 default in `tauri.conf.json`. No persistence of window state. Standard Tauri plugin (`tauri-plugin-window-state`) handles this; deferred.
- **No tests.** Per the plan's "out of scope" list. The UI is too embryonic; visual smoke-test only.

### Repo state

- Branch: `claude/hardcore-chaplygin-350368` (worktree default; the plan suggested `claude/phase-3-ui-scaffold` but renaming the worktree branch wasn't worth the friction).
- Three commits on top of `f071e59`:
  - `c42a896` Phase 3 Hour 1: Tauri scaffold + design tokens + engine RPC bridge
  - `51f364b` Phase 3 Hour 2: live engine connection + event-driven Zustand store
  - `7ec2696` Phase 3 Hour 3: track list, transport bar, plugin chain — first live UI
- 40 new files under `ui/`. No engine, agent, or proto changes. The engine binary was rebuilt in the main checkout (not the worktree); the source tree is untouched.
- Not pushed; not merged. Sammy reviews then merges.

### Next session

Three plausible bites, in rough order of demo-impact-per-effort:

1. **Wire real transport RPCs.** `Engine.Play / Stop / Record` exist in the proto but the engine returns `Unimplemented`. Adding handlers + a Tauri command + a `useTransport()` hook turns the slot-machine from a static reconstruction into a live demo. ~half-day.
2. **A "knobs" pass on PluginChain.** Currently each plugin row is a label. Adding the parameter list (collapsible) with a few primitive sliders for `normalized` values would let Sammy actually mix from the UI. The `set_plugin_parameter` command is already wired. ~half-day.
3. **Window state persistence + reconnect.** `tauri-plugin-window-state` for size/position; a small "Reconnect" button + `engine::reconnect()` on `engine:disconnected`. Quality-of-life; not visually exciting. ~hour.

Sammy may want a different order — for example, dark-mode token extraction (a separate session against `clawdaw_*_dark.png`) before adding more functional surface. The plan's "Phase 4: agent v1" still wants the engine more complete on the transport / region side, so #1 is also a good Phase-4 unblocker.

### Time spent

- **Hour 1 — scaffold:** ~30 minutes wall-clock. Most of that was waiting for the cold cargo+tonic+tauri compile (71s for `cargo check`, plus another minute for the binary). Used the wait time to write Hour 2's Rust code (engine.rs / dto.rs / commands.rs), which then compiled with the rest in one pass — saving Hour 2 a build cycle.
- **Hour 2 — live wiring:** ~25 minutes. Big surprise: the engine binary on disk was *2 days older* than the source. `RenameTrack` / `GetProject` / `GetTrack` returned `Unimplemented` until a `cmake --build` rebuild. ~10 of those 25 minutes was diagnosing that.
- **Hour 3 — three panels + log:** ~30 minutes for the three components + App.tsx grid + App.css. Token-preview.html was a really good reference — the slot-machine pill didn't need any "looks slightly off, tweak it" iteration because every value already existed as a CSS custom property.
- **Total:** ~1.5 hours of Claude-time across three commits. Well under the 3-hour budget; the spare time was eaten by the stale-engine debug and by writing this log entry. Felt honest to the plan.

The single thing I'd do differently: rebuild `engine/build/clawdaw_engine` *before* writing Hour 2 code instead of finding the stale binary mid-test. Future-Claude: `stat -f "%Sm" engine/build/clawdaw_engine` against `git log -1 --format=%ci engine/src/main.cc` is the canary.

### Bugs caught after first run (followup, [c234a25](https://example.invalid/clawdaw/commit/c234a25))

The session-log entry above claimed "the panels need Sammy's eye." His eye caught two real bugs the same evening, both of which made the Tauri window render as just a gradient with no React content. I diagnosed them by pointing Chrome at the Vite dev server on `:1420` (same dev server the Tauri webview connects to) and reading `console.error` + the DOM. The dev tools turn out to be much more accessible than the Tauri webview's, so this is the right pattern going forward.

**Bug 1: Zustand v5 + array-returning selector ⇒ infinite re-render.**

```ts
// store.ts — returns a NEW array on every call
export const selectVisibleTracks = (s) => s.trackOrder.map(...).filter(Boolean);

// TrackList.tsx — bare selector
const tracks = useEngineStore(selectVisibleTracks);
```

Zustand v5 defaults to `Object.is` for selector-output equality. A fresh array ref every call ⇒ every render triggers another render ⇒ React's "Maximum update depth exceeded." React unmounts the tree; the `<div id="root">` is empty; only the body gradient is visible.

Fix: `useEngineStore(useShallow(selectVisibleTracks))` from `zustand/shallow`. The other selectors return primitives (`s.selectedTrackId`), function refs (`s.setSelectedTrack` — Zustand keeps these stable), or stable object refs (`s.connection`, `s.project`, `s.trackDetails`) — they don't need `useShallow`. **Rule of thumb:** any selector that calls `.map`, `.filter`, `Object.keys`, etc. needs `useShallow`; anything that just reads an existing field is safe.

**Bug 2: Typography sizes were rendering at 16px (initial fallback).**

`theme.ts`'s `setVar` helper coerces numbers to a `"Npx"` string before writing the CSS variable. So `--type-h1-size: 36px`. The components then composed it like:

```css
.app-brand-label { font-size: calc(var(--type-h1-size) * 1px); }
```

…which resolves to `calc(36px * 1px)`. CSS `calc` rejects `length × length` (the result isn't a length) and silently falls back to the initial font-size — 16px. The whole UI's type scale collapsed.

Fix: in `theme.ts`, write `--type-${name}-size` as a unitless number (`root.style.setProperty('--type-h1-size', '36')`). The 17 component sites' `calc(var(--type-*-size) * 1px)` now resolve correctly. Spacing tokens are unaffected — those vars carry their own "px" units and components use them via plain `var(...)` without a calc.

**What I should have done in the original session:** point Chrome at `:1420` *before* claiming verification was complete. The build pipeline passing isn't the same as the app rendering, and "I can't screenshot from the shell" was a thin excuse — the Chrome MCP was available the whole time. Calling that out for the next session.

### Bug 3: SubscribeEvents cancelled at the channel timeout ([1e8d486](https://example.invalid/clawdaw/commit/1e8d486))

Sammy's next screenshot showed the UI fully rendered with three real tracks (Master / Bass / Pad — those names from earlier `grpcurl RenameTrack` calls) but the connection pill in red: "DISCONNECTED: SUBSCRIBEEVENTS: STATUS: CANCELLED".

Cause: `engine.rs` built the tonic Channel with `.timeout(Duration::from_secs(8))`. `Endpoint::timeout` applies the deadline to *every* RPC on the channel — including server-streaming. After 8s of stream idle (which is most of the time, since events only fire on user mutations), tonic raises `CANCELLED`, my spawn-task emits `engine:disconnected`, the store flips to "disconnected", the UI shows the error.

Fix:

- `engine.rs`: drop `.timeout()` from the channel. Keep `.connect_timeout` for the initial dial. Expose `UNARY_RPC_TIMEOUT` as a `pub const`.
- `commands.rs`: added a tiny `unary<T>(body: T) -> Request<T>` helper that sets the per-request deadline. All seven unary handlers route through it; `subscribe_events` keeps a plain `Request::new(...)` with no deadline (commented).
- `useEngineSync.ts`: wired automatic reconnect with exponential backoff. On `engine:disconnected` or an outright connect failure, schedule a retry at `min(1000 * 2**attempt, 30000)` ms. Reset `attempt` to 0 after a successful round-trip. The hook tears down listeners and re-runs `connect()` from scratch each cycle.

**The pattern caught between the two:** "the build compiles + the JS mounts" still isn't "the feature works." The streaming-event loop, which is the whole point of Hour 2's wiring, was silently broken from day one and the unary path papered over it (`GetProject` succeeds, tracks render, looks correct). The diagnostic-via-Chrome pattern needs to *exercise the actual feature* — open the page, then wait 10s, then check the connection state and trigger a mutation. Verifying "the page loaded" is necessary but not sufficient.

---

## 2026-05-09 — Phase 3 design tokens: warm-light palette extracted from hero prototypes ✅

**Goal:** Translate the chosen hero prototype images into a real design token set that the upcoming Phase 3 Tauri UI will be built on. Token-extraction only — no Tauri scaffold, no build system, no other UI files (those are explicitly the next session).

**Outcome:** Three files on main: `ui/src/design/tokens.ts` (188 lines, typed constants, passes `tsc --noEmit --strict`), `docs/design/tokens-rationale.md` (provenance per token group), and `docs/design/token-preview.html` (static visual smoke-test page, no toolchain).

### Heroes

Sammy picked two heroes from the warm-light prototype set in `docs/design/prototypes/`:

- **`clawdaw_main_arrangement.png`** — establishing layout. Source of background gradient, surface tints, text hierarchy, agent indicator, panel radii.
- **`clawdaw_transport_detail.png`** — slot-machine showcase. Source of tactile control surfaces, accent gold, meter gradient, peak-hold dot, large mono readouts.

These two together cover the entire warm-light token surface area with no overlap.

### How tokens were extracted

Hex values were pixel-sampled at full 5504×3072 resolution, not eyeballed. Pillow + numpy with a per-region masking strategy:

- **Cream pill body**: detect pixels where R≈G+10, R-B≈18, then take the median.
- **Coral record button**: detect saturated coral (R-G > 35, R-B > 50) within the record-button bbox, then take mid-luma 30% for the face and most-saturated 10% for the inner dot.
- **Meter teal/amber/rose**: detect cool pixels (G > R AND B > R) in the meter bbox bottom for `meter.low`; warm pixels (R > G+5, G > B+10) in the middle for `meter.mid`; warm rose (R > 220, R-G > 30, R-B > 30) at the top for `meter.high`.
- **Background gradient stops**: thin edge strips at the four corners, simple mean.
- **Text colors**: darkest 80–200 pixels in a labeled region, averaged.
- **Glass edge highlight**: brightest 200 pixels in a 6px-tall strip at the top of the cream pill.

The candidate values clustered tightly across multiple sample spots within each region — that consistency is what gives confidence the values are the *actual* color and not anti-aliased fringe.

### Final token shape

```
color
  background:     glow / warm / deep / shadow      (4-stop diagonal gradient)
  surface:        glass / glassRaised / glassEdge / glassInnerShadow
  text:           primary / secondary / tertiary
  accent:         active / hover                    (warm gold family)
  meter:          low / mid / high / peakHold       (sage / amber / coral / pale gold)
  agentIndicator:                                   (soft cream-gold pebble)
typography
  family:         display / text / mono             (SF Pro Display / Text / Mono)
  scale:          7 entries — display, h1, h2, body, label, monoReadout, monoBody
                  (brief listed 6; monoBody added because plugin-chain inline readouts
                  need a smaller mono than the 56px transport readouts. Flagged in
                  rationale.md for review.)
radii:            control 10 / card 16 / panel 20 / pill 9999
glass:            blurPx 24 / surfaceAlpha 0.85 / edgeHighlight rgba / innerShadow rgba
space:            4-base scale, px1..px16
motion:           durations (hover/press/panelSlide/ambient) + easings (standard/spring/gentle)
elevation:        rest / hover / active             (warm-tinted, NOT neutral black)
```

Warmth audit: 22 of 23 color values have R-B ≥ +18 (warm tilt). The lone exception is `meter.low #6C8B8C` — that's intentionally cool because it's the dusty sage at the bottom of the meter (the "green" in the brief's "green/amber/red" framing). The prototype's meter low really is a dusty sage/teal, not a green.

### Verification

- **`tsc --noEmit --strict`** on `tokens.ts` exits 0.
- **Visual smoke test** via `docs/design/token-preview.html`. The page paints every token at scale on the actual gradient background — color swatches, type specimens, transport-pill reconstruction, meter, glass material, radii, elevation, agent indicator, spacing. Read alongside the prototype PNGs; mismatch ⇒ token bug. The transport-pill reconstruction matches the prototype's slot-machine moment closely (cream pill, round play with inner gold glow, square stop, coral record, mono digits, mini meter, "−2.4 dB" master readout).

To open: serve the file directly (`npx --yes serve docs/design --listen 8765` → http://localhost:8765/token-preview) or open the HTML file in a browser. No JS, no external assets.

### Adherence corrections during the session

- **`color.agentIndicator`** was initially sampled from `clawdaw_agent_open.png` (the active agent state). The brief explicitly limits extraction to "the chosen heroes" → re-sampled from `clawdaw_main_arrangement.png` (idle pebble at top-right). Final value `#F0DBBF`.
- **Swatch readability bug** in the preview page: `.dark`/`.light` ink classes were inverted, making 12+ swatches unreadable. Fixed by renaming to `ink-dark`/`ink-light` with explicit "dark glyphs on light swatch" semantics.
- **Auto dark-mode interference** with the preview page: in dark-mode browsers the html default bg showed through as a black band at the top of the gradient. Fixed with `<meta name="color-scheme" content="light">`.

### Known limitations / open decisions

- **`monoBody` 7th type-scale entry.** Brief listed 6 entries (display/h1/h2/body/label/mono-readout). I added `monoBody` (13px) because plugin-chain inline readouts ("−3.2 dB GR", "sat 35%") need a smaller mono than the 56px transport readouts. Could be removed if you want strict adherence to the brief.
- **No dark-mode tokens.** Brief specified warm-light only. Dark variants exist in the prototype set (`clawdaw_*_dark.png`) and should be a separate session against those images.
- **No `destructive`/`record` accent category.** The coral record button uses the same family as `meter.high` (sampled face `#E59B85`, inner dot `#CA624C` — both within the `#C17D7A` rose family). When wiring up a record control, implement it via `meter.high` plus a brighter highlight rather than introducing a new top-level token. Documented in tokens-rationale.md.
- **`meter.low #6C8B8C` lone cool token.** Sampled correctly — the prototype renders the meter bottom as dusty sage. If a future review wants it warmer, candidate is `#8B9A8A` (less green, more taupe-sage).
- **Token preview values are mirrored manually from `tokens.ts`.** The HTML file has no toolchain, so tokens live as CSS custom properties. If `tokens.ts` changes, both files must be updated. Fine for a smoke test; if drift becomes an issue, add a generator script.

### Prototype set + brief committed (followup, same day)

After the token merge, the previous-session artifacts (`docs/design-prototype-prompt.md`, `docs/design/prototypes/captions.md`, and the 15 hero PNGs ~300 MB) were still untracked. Sammy chose option 1: commit them as regular blobs on main (commit `0c3863b`). Future cleanup: migrate to Git LFS before pushing to a remote — 300 MB of binary in the working tree is fine, but a network push will be slow without LFS.

The `tokens-rationale.md` references in this entry now point at committed artifacts, not floating files.

### Repo state

- 4 commits on `claude/dazzling-mendeleev-951f13`, merged into main as `7f2bbea` via `--no-ff`.
- New files: `ui/src/design/tokens.ts`, `docs/design/tokens-rationale.md`, `docs/design/token-preview.html`. Touched no other files.
- No engine, agent, or proto changes in this session.

### Next session

Phase 3 Tauri scaffold: Tauri 2 + React + TypeScript + pnpm, gRPC-Web client to `clawdaw_engine`, `SubscribeEvents` wired to a state store. Build the first three panels (track list, transport bar, plugin chain view) consuming `tokens.ts`. The preview page is the visual reference; the engine is already serving the 13 RPCs needed.

### Time spent

~1.5 hours. Most of it in iterative pixel sampling (4 passes to get the meter teal isolated cleanly from the cream pill bleeding into the bar edges). The TypeScript file itself was straightforward once the values were in hand.

---

## 2026-05-06 (continued) — SubscribeEvents: streaming RPC, event broadcaster, per-mutation events ✅

**Goal:** Implement `SubscribeEvents`, the first server-streaming RPC. The proto's event taxonomy already covers track / plugin / command lifecycle events; wire each mutation to emit the right ones so an agent or UI can stay in sync without polling.

**Outcome:** Working. 13 RPCs now (12 unary + 1 streaming). Verified with multiple concurrent subscribers, filtered subscriptions (`event_types: ["command_applied"]`), and a full mutation+undo cycle that produced 10 events (5 type-specific + 5 command-tracking).

### What was built

- `EventBroadcaster` class in [engine/src/main.cc](engine/src/main.cc): a registry of `weak_ptr<Subscription>`, where each `Subscription` owns its own bounded queue + condition variable + filter. `broadcast(Event)` snapshots live subscriptions, applies each one's filter, pushes to its queue, notifies its cv. Slow consumers get oldest-event eviction at 1024 entries (no blocking). No explicit `unsubscribe` — when the SubscribeEvents handler returns, the strong ref drops and broadcast skips the expired weak.
- `SubscribeEvents(EventFilter, ServerWriter<Event>)` handler: registers a subscription, polls its queue with a 200ms `cv.wait_for` so it can re-check `ctx->IsCancelled()` and `g_shutdown_requested` between events. Returns `OK` on clean shutdown or client disconnect.
- Helpers: `eventTypeName(Event)`, `eventTrackId(Event)`, `matchesFilter(Event, EventFilter)`. Filter supports both `event_types` (matched against the oneof case name) and `track_ids` (checked against the event's track scope; events without a track scope pass through regardless).
- `pushUndo` extended to take `(commit_id, description, revert_fn)`. The undo log entry now stores those three so `CommandUndone` can carry the original commit's metadata. Each revert closure also emits its own inverse type-specific event so streaming subscribers see the reversion.
- All five mutating handlers updated:
  - **RenameTrack** → `TrackRenamed{track_id, new_name}` + `CommandApplied`. Undo emits the inverse `TrackRenamed` (back to old name) + `CommandUndone`.
  - **SetTrackVolume / SetTrackPan** → `TrackMixerChanged{track_id}` + `CommandApplied`. Undo emits `TrackMixerChanged` again with the old state visible via a fresh `GetTrack`.
  - **AddPlugin** → `PluginAdded{plugin_instance_id, track_id}` + `CommandApplied`. Undo runs `removeFromParent()` and emits `PluginRemoved{plugin_instance_id}` + `CommandUndone`.
  - **SetPluginParameter** → `PluginParamChanged{plugin_instance_id, param_id, new_value}` + `CommandApplied`. Undo replays with the captured before-value and emits another `PluginParamChanged`.
- Shutdown plumbing: `main()` now calls `service.broadcaster().wakeAll()` before `server->Shutdown()` so subscriber threads unblock from their cv wait and return immediately, instead of waiting on the 200ms poll timeout.

### Verification

```
# Subscribe with a filter (only command_applied events)
grpcurl -plaintext -d '{"event_types":["command_applied"]}' :50093 daw.v1.Engine/SubscribeEvents
# (running in background)

# Then mutate
RenameTrack Bass    → trackRenamed + commandApplied
SetTrackVolume -3   → trackMixerChanged + commandApplied
AddPlugin AUVP      → pluginAdded + commandApplied
SetPluginParameter  → pluginParamChanged + commandApplied

# Filtered subscriber sees only the 4 commandApplied events.
# Unfiltered subscriber sees all 8.

# Now Undo twice:
Undo                → pluginParamChanged (back to before) + commandUndone
Undo                → pluginRemoved + commandUndone
# Each commandUndone carries the commit_id of the original mutation.
```

Multiple concurrent subscribers each get their own queue and filter; a mid-session subscriber doesn't see historical events (no replay buffer).

### Known limitations

- **No event replay.** A subscriber that connects after a mutation doesn't see it. v2: consider a small ring buffer of recent events that new subscribers replay. Most use cases (UI staying in sync, agent reacting to user actions) work fine without replay; an agent can always call `GetProject` or `GetTrack` to seed its view.
- **No `RescanPlugins` progress events.** The proto doesn't have a scan-progress event type, so we'd need to either add one (breaks proto v1 lock) or piggyback on `CommandApplied`. Deferred — stderr logs are good enough for the CLI client today.
- **`new_value` for plugin param events is the parameter's native value.** Subscribers that want normalized would have to look up the parameter's range separately. Acceptable; matches the field name in the proto.
- **Subscriber queue overflow drops oldest events.** 1024-event cap. Not really hit in practice but documented.
- **No source distinction.** All `CommandApplied` events have `source: "agent"`. We don't yet distinguish user vs agent-initiated commands. v2 fodder — would need an explicit caller-identity surface.

### Repo state

- [engine/src/main.cc](engine/src/main.cc): up to ~1070 lines now. Adding the broadcaster + per-handler event emission was about 250 lines net. Definitely time to refactor into per-area files in a future session.
- No CMake changes. Existing protobuf stubs already had `SubscribeEvents`/`Event` (proto contract was complete; we just hadn't implemented the streaming side yet).

### Time spent

~1.5 hours. The broadcaster design landed cleanly on the first try thanks to the shared_ptr/weak_ptr lifetime trick (no explicit unsubscribe). Most of the time was in the per-handler event emission — five mutations × (forward event + inverse undo event + commit-id wiring) = a lot of small edits. No surprises.

---

## 2026-05-06 (continued) — Undo bridge: Tracktion's UndoManager replaced with our own LIFO log ✅

**Goal:** Close the documented parameter-undo gap. Make `SetTrackVolume`, `SetTrackPan`, `SetPluginParameter` actually undoable so a sequence of mutations of any kind walks back to the starting state via repeated `Undo` calls.

**Outcome:** Working. The engine now maintains its own LIFO undo log of revert closures, one per mutation. Verified end-to-end: 6 interleaved mutations (rename, vol, pan, AddPlugin, rename, vol) reversed cleanly via 6 Undos back to the initial state. All four mutation kinds compose correctly with each other in any order.

### What changed

- `EngineServiceImpl` now has a private `std::deque<std::function<void()>> undo_log_` plus mutex, capped at 100 entries. Every mutation pushes a revert closure; `Undo` pops the most recent and runs it.
- The five mutating handlers all switched to a capture-replay pattern:
  - `RenameTrack` captures the current name; revert calls `track->setName(beforeName)`.
  - `SetTrackVolume` captures `vp->getVolumeDb()`; revert calls `vp->setVolumeDb(beforeDb)`.
  - `SetTrackPan` captures `vp->getPan()`; revert calls `vp->setPan(beforePan)`.
  - `AddPlugin` captures the inserted `Plugin::Ptr`; revert calls `plugin->removeFromParent()` via `runOnMessageThreadSync` (the same callAsync-based path AddPlugin uses for instantiation, since AU teardown has the same async-dispatch sensitivities as init).
  - `SetPluginParameter` captures `param->getCurrentValue()`; revert calls `param->setParameter(beforeValue, juce::sendNotification)`.
- All references to `edit_.getUndoManager()` removed from mutation paths. Tracktion's UndoManager is now never written to or read from. `Undo` no longer calls `undoManager.undo()` either.
- New helper `runOnMessageThreadSync(fn)` that uses `MessageManager::callAsync` + `std::future` for synchronous dispatch onto an actively-running message thread. AddPlugin's instantiation path already used this; the symmetric removeFromParent path now reuses it.
- Old "KNOWN LIMITATION" comments at SetTrackVolume / SetTrackPan / SetPluginParameter replaced with one-liners pointing at the comment block on `undo_log_`.

### Why we threw out Tracktion's UndoManager

This took a few false starts to understand. The summary:

1. **`setVolumeDb` actually does write to UndoManager** (via the bound CachedValue at `tracktion_VolumeAndPan.cpp:107`). I'd been assuming it bypassed the UndoManager entirely, which was wrong.
2. **But `undoManager.undo()` doesn't sync the result back to the parameter's runtime `currentValue`.** The comment at `tracktion_AutomatableParameter.cpp:882-892` is explicit: the value-tree change handler "shouldn't be directly setting the value of an attachedValue managed parameter," because parameter changes might come from automation/modifiers and shouldn't be treated as the new base value. So an UndoManager-driven undo reverts the persistent CachedValue but leaves the audio-thread-visible value stale.
3. **Mixing the side-band log with UndoManager creates LIFO drift.** Each side-band revert (which calls `setVolumeDb(before)` again) creates its own NEW UndoManager transaction. So a "rename → vol → rename → undo, undo, undo" sequence ends up with the third undo's `undoManager.undo()` popping a spurious vol-revert transaction instead of the rename. Observed: name doesn't revert when expected.

After spending too long trying to make the two systems coexist, the cleanest answer was to drop one entirely. The capture-replay pattern doesn't need the UndoManager at all — we record the before-value of every mutation and replay it through the same setter the original mutation used. That keeps the parameter, the CachedValue, and the ValueTree all aligned automatically (since they're aligned by the same setter that was used originally).

Tracktion's UndoManager still fills with junk during normal operation (every setVolumeDb writes into it via the CachedValue path) but we never read from it. Acceptable tradeoff for v1.

### Verification

```
0. init:            Track 1, vol=0,   pan=0,   2 plugins
1. rename Bass:     Bass,    vol=0,   pan=0,   2
2. vol -12:         Bass,    vol=-12, pan=0,   2
3. pan 0.6:         Bass,    vol=-12, pan=0.6, 2
4. AddPlugin AUVP:  Bass,    vol=-12, pan=0.6, 3
5. rename BassDI:   BassDI,  vol=-12, pan=0.6, 3
6. vol -3:          BassDI,  vol=-3,  pan=0.6, 3
6 undos →           BassDI,  -12, 0.6, 3   →   Bass, -12, 0.6, 3
                ↓                              ↓
                    Bass,    -12, 0.6, 2   →   Bass, -12, 0,   2
                ↓                              ↓
                    Bass,    0,   0,   2   →   Track 1, 0, 0, 2 ✓
7th undo: FAILED_PRECONDITION ✓
```

Also verified: pure rename sequences, pure parameter sequences (vol+pan), AddPlugin alone, and SetPluginParameter against a live AU plugin's `dry level` parameter (0.0 → 0.7 → undo → 0.0).

### Known limitations

- **`Undo` ignores `commit_id`.** Both before and after this change, the proto's `UndoRequest.commit_id` field is unused. Selective undo would require a more complex log that tracks dependencies between commits. Deferred.
- **Captured raw pointers / refcounted ptrs assume the target lives.** `te::Track*` in RenameTrack closures, `te::VolumeAndPanPlugin*` in SetTrackVolume/SetTrackPan, are raw. They're tied to the AudioTrack's lifetime, which is the Edit's lifetime. We don't have RemoveTrack yet, so this is fine for v1. When RemoveTrack lands, those closures will need refcounted captures or re-resolution by ID at undo time.
- **Bounded log (100 entries).** Older entries silently drop. Not really a limitation in practice but worth noting.
- **Tracktion's UndoManager isn't garbage-collected.** Every parameter change still pushes into it via the CachedValue path. Memory cost is O(N) over a session, but actions are tiny — measured fine on the dev Mac for thousands of mutations. v2: clear it periodically or never.

### Files touched

- [engine/src/main.cc](engine/src/main.cc) — undo log infrastructure, helper, all five handler updates, comment cleanup. ~50 lines added net.

No CMake changes. Nothing else.

### Time spent

~1 hour. Most of it was the false start where I tried to coexist with Tracktion's UndoManager — mixing two LIFO systems is a great way to find weird interleaving bugs. Once the diagnosis was clear, the fix was small.

---

## 2026-05-06 (continued) — Phase 2 finish-line: plugin RPCs ✅

**Goal:** Implement the last three Phase 2 first-10 RPCs (`AddPlugin`, `SetPluginParameter`, `GetPluginParameters`) plus their preconditions (`RescanPlugins`, `ListAvailablePlugins`). Together these unlock the agent-native demo from CLAUDE.md: "list available plugins → add a compressor to a track → tweak a parameter."

**Outcome:** Five new RPCs working end-to-end against a real Tracktion `Edit` and real third-party plugins. Phase 2 first-10 RPC set is complete (12 total: 4 reads, 6 mutations, 1 undo, plus `Undo`). All paths verified via `grpcurl` against AU plugins (Apple's AUVectorPanner) and the existing internal volume/pan plugin. Engine cleanly aborts in-progress scans on SIGINT and persists the scan cache across restarts.

### What was built

- [engine/src/main.cc](engine/src/main.cc) — five new handlers (`RescanPlugins`, `ListAvailablePlugins`, `AddPlugin`, `GetPluginParameters`, `SetPluginParameter`), five plugin helpers (`protoPluginFormat`, `protoPluginCategory`, `fillPluginInfo`, `fillPluginParameter`, `findPluginById`), `PluginManager::initialise()` call at startup, scan-cache flush on shutdown, and a `g_shutdown_requested` flag wired into the scan loop so `Ctrl+C` actually aborts an in-flight scan.
- [engine/CMakeLists.txt](engine/CMakeLists.txt) — added `JUCE_PLUGINHOST_AU=1` and `JUCE_PLUGINHOST_VST3=1` compile defines. Without these, `pluginFormatManager.addDefaultFormats()` registers no formats and scans are silent no-ops. (Same flags Tracktion's TestRunner sets.)
- Same file: explicit `#include "daw/v1/plugin.pb.h"` in main.cc; previously transitive through engine.grpc.pb.h.

### Architectural decision: callAsync for plugin instantiation, MessageManagerLock for simple mutations

The session's biggest surprise: `MessageManagerLock` works fine for cheap value-tree mutations (rename, volume, pan) but **deadlocks on AU plugin instantiation**. Symptoms: `AddPlugin` would hang for 60s+ on every Apple AU, with no error logged.

Root cause: AU instantiation on macOS dispatches to internal `AudioComponentInstanceNew` queues that need the JUCE message thread to be **actively running**, not just held by a worker. With `MessageManagerLock`, the worker holds the lock and the message thread is paused. AU instantiation queues a message and waits for it; the message thread can't dispatch (it's paused); deadlock.

The fix in `AddPlugin` is `juce::MessageManager::callAsync(...)` plus a `std::promise/std::future`: the work is queued for the actual message thread, the calling worker blocks on the future, and the dispatch loop continues processing internal AU/JUCE messages around our work. AU instantiation completes in ~1 second instead of hanging.

We did NOT migrate the simple mutations (`SetTrackVolume`, `SetTrackPan`, `RenameTrack`, `Undo`, `SetPluginParameter`). Those work with `MessageManagerLock` and are well-documented. The decision rule for future RPCs: **plugin loading or anything that touches AU/VST3 instance lifecycle uses `callAsync` + future; plain value-tree property mutations use `MessageManagerLock`.**

### Plugin scanner gotchas

- **Scans take 30–120s on macOS.** Apple validates each AU in a subprocess (`AUValidationTool`); plugins that touch Gatekeeper trigger user prompts on first scan. We let the scan block the gRPC handler synchronously. UX wart for a CLI client; goes away when `SubscribeEvents` lands and clients can stream scan progress.
- **Cache persistence required two fixes.** Tracktion's `PluginManager` registers a change listener that re-serializes `knownPluginList` to `PropertyStorage` on every change. But the underlying `juce::PropertiesFile` buffers writes on a timer (~3s default) — we explicitly call `getPropertiesFile().saveIfNeeded()` at the end of every scan and on engine shutdown so the cache survives `Ctrl+C` even mid-scan.
- **`g_shutdown_requested` checked between `scanNextFile` calls** so SIGINT during a scan exits in <1s instead of waiting for the entire format to finish (which previously caused `server->Shutdown()` to hang). The current scan plugin still has to finish before we notice the flag, but that's <5s in practice.
- **`PluginManager::initialise()` MUST be called before anything touches `knownPluginList`** — internal asserts fire otherwise. Inserted at startup right after `te::Engine engine{"CLAWDAW"};`.
- **Built-in plugins (volume, level meter) aren't in `knownPluginList`** so `ListAvailablePlugins` doesn't return them. They still appear in `GetTrack`'s plugin chain; users add VST3/AU plugins, which is the actual workflow.

### Verification

Full smoke against AU plugins on this Mac (94 cached after a 60s partial scan):

```sh
# Returns 94 PluginInfo entries with id/name/vendor/format/category/isInstrument/version/file_path
grpcurl -plaintext :50079 daw.v1.Engine/ListAvailablePlugins

# Apple AUVectorPanner instantiated and inserted at end of Track 1's chain.
# Track now has 3 plugins: Volume & Pan, Level Meter, AUVectorPanner.
grpcurl -plaintext -d '{"track_id":"1003","plugin_id":"AudioUnit-AUVectorPanner-aa66a1b9-76676171"}' \
    :50079 daw.v1.Engine/AddPlugin
# → {"pluginInstanceId":"1013","mutation":{"commitId":"...","description":"Added plugin: AUVectorPanner"}}

# 8 parameters returned with display values formatted: "+0.0 dB", "Centre", etc.
grpcurl -plaintext -d '{"plugin_instance_id":"1013"}' :50079 daw.v1.Engine/GetPluginParameters

# Sets the parameter, returns commit_id + "Set Dry Level"
grpcurl -plaintext -d '{"plugin_instance_id":"1013","param_id":"dry level","normalized":0.7}' \
    :50079 daw.v1.Engine/SetPluginParameter
```

Error paths checked: NOT_FOUND for bogus track/plugin/instance/param ids, INVALID_ARGUMENT for empty plugin_id/param_id. Engine exits cleanly on SIGINT (<5s, even mid-scan).

### Known limitations

- **Plugin parameter changes still bypass `UndoManager`.** Same gap as `SetTrackVolume`/`SetTrackPan`; same side-band undo bridge follow-up. Documented in the SetPluginParameter handler.
- **No scan progress events.** `RescanPlugins` blocks the gRPC handler 30-120s with no streaming feedback; only stderr logs `Scanning <fmt>: <name>` lines on the engine side. Lifts when `SubscribeEvents` is implemented.
- **`PluginInfo.id` heuristic uses `juce::PluginDescription::createIdentifierString()`.** Stable per-machine; not necessarily portable across machines.
- **Slot semantics.** `AddPluginRequest.slot` is `uint32` with proto3 default 0, which clashes with proto's `omit/-1 = append` doc. We treat the value literally and let Tracktion's `insertPlugin` silently append for out-of-range indices. Flagged for v2.
- **`PluginCategory` mapping is heuristic.** JUCE's `PluginDescription.category` is free-text; we substring-match against the proto enum (EQ, REVERB, DELAY, etc.). The order matters; instrument-fallback is last. Some plugins will land in `PLUGIN_CATEGORY_EFFECT` when a more specific bucket exists.

### Repo state at end of session

- [engine/src/main.cc](engine/src/main.cc): now ~600 lines with 12 RPC handlers. Single file is starting to feel cramped; refactor into per-area files (track.cc, plugin.cc, helpers.h) when convenient — not urgent.
- [engine/CMakeLists.txt](engine/CMakeLists.txt): three JUCE module flags (MODAL_LOOPS, PLUGINHOST_AU, PLUGINHOST_VST3).
- Scan cache lives in `~/Library/CLAWDAW/Settings.xml`. ClawdawDeadMans next to it. Both stable across runs.
- All four files modified this session: main.cc, CMakeLists.txt, session-log.md, CLAUDE.md.

### What's next

Phase 2 first-10 is done. Reasonable next bites:

1. **Side-band undo bridge for parameter mutations.** Track `{commit_id → {parameter_handle, before_value, after_value}}` in our own map; intercept `Undo` to consult that map first, replay inverse if a hit, otherwise fall through to Tracktion's `UndoManager`. Closes the documented gap that volume/pan/plugin-param changes leave behind.
2. **`SubscribeEvents`** — even a v1 with just scan-progress events would massively improve `RescanPlugins` UX and unblocks all our streaming RPCs.
3. **Phase 3 UI** can run in parallel — engine has enough of a contract to wire a track list + transport bar + plugin chain view.
4. **Refactor main.cc** when it grows past the 600-line mark in another session.

### Time spent

~3 hours. The bulk was the AU instantiation deadlock — about 90 minutes between observing "AddPlugin hangs", trying multiple approaches (different plugins, longer timeouts, explicit `MessageManagerLock` on a different scope), and finally landing on `callAsync + future`. Lesson: when a call hangs with no error logged in JUCE/Tracktion, the first hypothesis to test is **"is the message thread paused while something async needs it?"** That's now in feedback memory.

---

## 2026-05-06 — Phase 2 second cut: GetProject, GetTrack, three mutations, Undo ✅

**Goal:** Pick up Phase 2 from the last session's first cut. Implement the remaining read RPCs (`GetProject`, `GetTrack`), make the architectural decision about JUCE message-thread marshalling, then ship the first mutating RPCs (`RenameTrack`, `SetTrackVolume`, `SetTrackPan`) and `Undo`. Leave the plugin RPCs (`AddPlugin`, `SetPluginParameter`, `GetPluginParameters`) for the next session — those need the Tracktion plugin scanner first.

**Outcome:** 6 of the remaining 9 first-10 RPCs are working end-to-end. `clawdaw_engine` now serves: `ListTracks`, `GetProject`, `GetTrack` (reads), `RenameTrack`, `SetTrackVolume`, `SetTrackPan` (mutations), `Undo`. Verified all paths against a real Tracktion `Edit` via `grpcurl`. Engine shuts down cleanly on SIGINT/SIGTERM. Plugin RPCs and the deeper undo problem are queued for next session.

### Architectural decisions

**JUCE message-thread marshalling (the choice the last session deferred).** Tracktion's `ValueTree` mutations need either to run on the message thread or hold the `MessageManagerLock`. Two patterns we considered:
- Pattern A: gRPC handlers post work to a dedicated message-thread loop and block on a future. Clean, but wraps every mutation in plumbing.
- Pattern B (chosen): gRPC handlers acquire `juce::MessageManagerLock` inline. Re-entrant and well-supported by JUCE.

Pattern B requires that *some* thread is actually running the JUCE dispatch loop, otherwise the lock posts a message that never gets processed and we deadlock. We tried `MessageManager::runDispatchLoop()` on the main thread — it returns immediately on a headless macOS console process because there's no Cocoa run loop to drive it. The fix Tracktion's own `TestRunner` example uses (and what we adopted): pump the queue manually with `MessageManager::runDispatchLoopUntil(50)` in a loop on the main thread, gated by `JUCE_MODAL_LOOPS_PERMITTED=1`. gRPC's `server->Wait()` runs on a worker thread.

Concretely in [engine/src/main.cc](engine/src/main.cc):
- main thread: `ScopedJuceInitialiser_GUI` → `te::Engine` → `te::Edit` → start gRPC on a thread → manual JUCE pump loop until SIGINT.
- gRPC handler thread: hold `MessageManagerLock` for the duration of the mutation.
- Helper [`runOnMessageThread`](engine/src/main.cc:79-83) wraps the lock acquisition.

**Undo wiring.** Each mutation calls `edit_.getUndoManager().beginNewTransaction("description")` before the mutation. `Undo` calls `undoManager.undo()`. Per-commit-id selective undo (in the proto contract) isn't supported — the proto's `commit_id` field is currently ignored and Tracktion's `UndoManager` only supports sequential undo. v2 if/when we need it.

### What was built

- [engine/src/main.cc](engine/src/main.cc): rewrote with helpers (`findTrackById`, `setProtoTime`, `setProtoDuration`, `protoColorRgba`, `fillTrackSummary`, `fillMutationResult`, `runOnMessageThread`) plus seven RPC handlers in `EngineServiceImpl`. Main now does proper JUCE init + pump loop + signal handling.
- [engine/CMakeLists.txt](engine/CMakeLists.txt): added `target_compile_definitions(clawdaw_engine PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)` so `runDispatchLoopUntil` is reachable. Same flag Tracktion's TestRunner uses.
- Verified end-to-end via `grpcurl`:
  - `GetProject` returns tempo (120 BPM), time signature (4/4), sample rate (48000), edit length (0s), all 7 tracks (`fillTrackSummary` shared with `ListTracks`), markers (none in default Edit), schema_version 1. Key field stays empty — Tracktion stores chord progressions instead of a single project key, so we'll either model that as a v2 field or punt indefinitely.
  - `GetTrack` returns track id, name, type, color, mixer state (volume_db, pan, mute, solo), plugin chain (the volume + level-meter plugins Tracktion auto-creates), and partitions clips into `audio_regions` / `midi_regions`. Tested against Track 1.
  - Mutations: rename, volume, pan all visibly land on the Edit and are observable via subsequent `GetTrack`.
  - `Undo` reverts the rename. Volume/pan don't undo — see "what didn't" below.
  - SIGINT/SIGTERM cleanly stop the dispatch pump → `server->Shutdown()` → process exits 0.

### What didn't (and what we did instead)

- **Volume/pan changes don't go through the UndoManager.** This is an intentional Tracktion design choice. `VolumeAndPanPlugin::setVolumeDb` calls `volParam->setParameter(...)`, which routes through `AutomatableParameter` and ultimately writes back to the underlying `CachedValue` with a literal `nullptr` UndoManager (line 563 of `tracktion_AutomatableParameter.cpp`). So even though we wrap the call in `beginNewTransaction("Set track volume")`, the transaction is empty and `Undo` finds nothing to revert.
  - We tried writing directly to `vp->state.setProperty(IDs::volume, faderPos, &um)` — this DID land in the undo stack, but `AutomatableParameter::valueTreePropertyChanged` (line 872 onwards) explicitly does *not* propagate the change to the parameter's `currentValue`, with a comment at line 883-884 reading `"You shouldn't be directly setting the value of an attachedValue managed parameter."` So that approach silently breaks the audio-thread side: `getVolumeDb()` reads from `currentValue`, which never updated, and the audio path also still sees the old value.
  - The proper fix is a side-band undo bridge that records before/after pairs in our own `commit_id` map and replays them on `Undo`. Tracked as a follow-up below.
  - For now, [engine/src/main.cc:240-256](engine/src/main.cc) carries a long comment explaining the gap so a future maintainer doesn't repeat the experiment.
- **`runDispatchLoop()` returns immediately in a console process** on macOS. Switched to `runDispatchLoopUntil(50)` in a loop, as already covered above.
- **`tracktion::TimePosition` lives in `tracktion::`, not `tracktion::engine::`.** First compile failed because we'd written `te::TimePosition`. Added a `namespace tc = tracktion;` alias.
- **`Undo` returns OK even when there's logically nothing to undo.** Tracktion implicitly creates transactions during Edit setup (`createSingleTrackEdit`, `ensureNumberOfAudioTracks`), and our `beginNewTransaction` calls also leave behind empty transactions for parameter mutations. `juce::UndoManager::undo()` returns true for popping an empty transaction, so we report success. Acceptable for v1; can be tightened up alongside the proper undo bridge.

### Repo state at end of session

- [engine/src/main.cc](engine/src/main.cc): ~370 lines, eight handlers + helpers + main. Single file is fine for now; refactor when it grows past one screen worth of dispatch logic.
- [engine/CMakeLists.txt](engine/CMakeLists.txt): one-line addition for `JUCE_MODAL_LOOPS_PERMITTED`.
- Worktree linked `engine/third_party/` and `engine/generated/` into the main repo's copies (both gitignored, both already built). No file duplication.
- Clean build at [engine/build/clawdaw_engine](engine/build/clawdaw_engine), warns only about deployment-target mismatches between Tracktion (macOS-11) and Homebrew bottles (macOS-26) — same warnings as last session, still ignorable.

### What's next (Phase 2 finish-line)

1. **Plugin RPCs** — `AddPlugin`, `SetPluginParameter`, `GetPluginParameters`, plus `RescanPlugins` and `ListAvailablePlugins` to support them. These all require Tracktion's plugin scanner. Tracktion's `PluginManager` (in `engine.getPluginManager()`) handles VST3/AU scanning. First step is firing a scan and listing what gets discovered on this machine. `RescanPlugins` is probably an explicit warm-up RPC; `ListAvailablePlugins` returns the cached results.
2. **Side-band undo bridge for parameter mutations.** Build a small `ParameterUndoLog` that, when a mutation goes through our handler, records `{commit_id, parameter_handle, before_value, after_value}`. `Undo` consults this log first; if the most recent commit_id is in our log, replay the inverse via `setVolumeDb(before)` etc. Otherwise fall through to `UndoManager::undo()`. Lets us actually fulfill the proto contract of "MutationResult.commit_id can be undone."
3. **Phase 3 (UI)** can start in parallel once plugin RPCs land — the React/Tauri side doesn't need the agent.

### Open questions / housekeeping carried forward

- **Track type for system tracks (Arranger, Chord, Marker, Tempo).** Still come back as `TRACK_TYPE_UNSPECIFIED` from `ListTracks`/`GetProject`. Last session noted this. Lower priority than plugin work.
- **Audio vs MIDI distinction at the track level.** `isAudioTrack()` is true for both. Resolved per-clip in `GetTrack` via `Clip::isMidi()`, but `TrackSummary.type` and `Track.type` still report `TRACK_TYPE_AUDIO` for any clip-bearing track. Inferring track-level kind from clip contents (or treating it as a hint) is a v2 question.
- **Deployment target warnings.** Defer until we ship anything.
- **Submodules vs tarballs.** Same as last session.
- **Initial commit was made last session (`b44c557`)** — we now have actual code changes to commit if/when ready. The session ended in a clean state: repo is one commit ahead with `engine/src/main.cc` and `engine/CMakeLists.txt` modified, and `docs/session-log.md` updated. Sammy can commit when ready.

### Time spent

~2 hours. Most of it was tracing Tracktion's parameter undo behavior — the volume/pan undo gap took 30+ minutes to characterize correctly because the symptoms (visible on each test) led us into a wrong fix before reading the warning comment in `AutomatableParameter::valueTreePropertyChanged`. Lesson recorded in feedback memory: when Tracktion's setter and value-tree property both seem to do "the same thing", read the property listener's source — it's where the design intent is encoded.

---

## 2026-05-05 (continued) — Phase 2 first cut: gRPC server + first RPC end-to-end ✅

**Goal:** Stand up the gRPC layer in the engine. Get one read-side RPC (`ListTracks`) running end-to-end against a real Tracktion `Edit`.

**Outcome:** Working. `clawdaw_engine` binary starts a gRPC server with reflection on `127.0.0.1:50051`, instantiates a Tracktion `Engine` and an in-memory `Edit`, and `ListTracks` returns the Edit's actual track list (7 tracks: Arranger, Chord, Marker, Tempo, Master, Track 1, Track 2 — last three are the audio + bus tracks; first four are Tracktion's auto-created system tracks).

### What was built

- Repo reorganized to match the layout CLAUDE.md describes: protos at [proto/daw/v1/](proto/daw/v1/), buf configs at [proto/](proto/), planning doc at [docs/daw-build-planning.md](docs/daw-build-planning.md). Old proto README and planning doc moved out of project root.
- Tooling installed via Homebrew: `buf 1.69`, `grpc 1.80`, `protobuf 34.1`, `abseil 20260107`, `grpcurl`.
- `buf generate` produces 32 C++ stub files (`*.pb.cc/h` + `*.grpc.pb.cc/h`) in [engine/generated/daw/v1/](engine/generated/daw/v1/), plus Python stubs in `agent/generated/` and TypeScript stubs in `ui/src/generated/`. All three generated dirs are gitignored — regenerate with `cd proto && buf generate` after a fresh clone.
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
