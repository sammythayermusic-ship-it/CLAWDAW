# clawdaw-agent

Python MCP server that exposes the [CLAWDAW](../README.md) audio engine to Claude as tools. Phase 4 v1: read project / track state, rename, set track volume + pan, add and tweak plugins, undo any mutation by passing back the `commit_id`.

The server is intentionally thin — every tool is a near-1:1 pass-through over the engine's gRPC RPC. Domain knowledge (what to mix, when to use which plugin) lives in Claude, not here.

## Prerequisites

- **Python 3.11+** (managed by `uv`).
- **`uv`** — install with `pip install --user uv` or `brew install uv`.
- **A running `clawdaw_engine`** on `127.0.0.1:50051`. From the repo root:
  ```
  ./engine/build/clawdaw_engine
  ```
  Binary missing or stale? Rebuild:
  ```
  cmake --build engine/build --target clawdaw_engine -j 8
  ```
- **Generated Python stubs** in `agent/generated/` (gitignored). Regenerate with:
  ```
  cd agent
  ./scripts/regen-proto.sh
  ```
  This calls the venv's `grpc_tools.protoc` against `../proto/daw/v1/*.proto` — not `buf generate`. The buf cloud `protocolbuffers/python` plugin currently emits gencode targeting protobuf 7.34.1, which has no matching runtime on PyPI; local generation keeps gencode and runtime in lockstep.

## Run the server

```
cd agent
uv sync
uv run clawdaw-agent
```

The server speaks the MCP stdio transport on stdin/stdout, logs to stderr. Point an MCP client (Claude Desktop, Claude Code, an mcp-cli probe, etc.) at it. Example Claude Desktop config:

```jsonc
// ~/Library/Application Support/Claude/claude_desktop_config.json
{
  "mcpServers": {
    "clawdaw": {
      "command": "uv",
      "args": ["--directory", "/abs/path/to/CLAWDAW/agent", "run", "clawdaw-agent"]
    }
  }
}
```

### Env vars

| Var | Default | Purpose |
| --- | --- | --- |
| `CLAWDAW_ENGINE_ADDR` | `127.0.0.1:50051` | gRPC target. |
| `CLAWDAW_LOG_LEVEL` | `INFO` | Python logging level for the agent process. |

## Tools

Thirteen, matching the engine's currently-implemented RPC surface:

**Reads** — `list_tracks`, `get_project`, `get_track`.

**Track mutations** — `rename_track`, `set_track_volume`, `set_track_pan`, `undo`. Each mutation returns a `commit_id`; pass it to `undo(commit_id=...)` to revert, or call `undo()` with no argument to revert the most recent mutation.

**Plugin** — `rescan_plugins`, `list_available_plugins`, `add_plugin`, `get_plugin_parameters`, `set_plugin_parameter`. Use `list_available_plugins` to discover the `plugin_id`s the engine knows about, then `add_plugin(track_id, plugin_id)` to insert one.

**Events** — `subscribe_events(n=20)` returns a snapshot of the last N events from a deque the agent maintains in the background (default capacity 256). Events are *not* streamed back through MCP — v1 is snapshot-only.

Track and plugin IDs are strings on the gRPC wire. The tools also accept integer forms (Claude often passes `1003` rather than `"1003"`) and coerce internally.

## Smoke tests

The integration test exercises a `get_project → rename_track → get_project → undo → get_project` round-trip against a live engine:

```
uv run pytest -m integration
```

The `-m integration` selector keeps it out of any future CI pytest run that doesn't opt in. With the engine not running it skips cleanly.

There's also a one-shot end-to-end probe over the MCP stdio transport:

```
uv run python scripts/mcp_smoke.py
```

It spawns `clawdaw-agent` as a subprocess, lists tools, runs the same rename+undo trace through the MCP layer, and prints the buffered events.

## Where to look next

- `src/clawdaw_agent/engine_client.py` — one method per RPC, with the per-call timeout discipline (5s on unary, *no* deadline on the streaming `SubscribeEvents`).
- `src/clawdaw_agent/server.py` — the FastMCP server. New RPCs slot in as new `@mcp.tool()` functions calling `_client(ctx).<rpc>(...)`.
- `../proto/daw/v1/engine.proto` — the contract.
- `../docs/session-log.md` — the latest entry covers what's deferred and why.
