"""MCP server exposing the CLAWDAW engine's 13 implemented RPCs as tools.

Tools are thin pass-throughs over `EngineClient`. Each mutation tool returns
the `commit_id` so the caller can `undo` it. `subscribe_events` returns the
last-N events from a buffer the agent maintains in the background — we do
not try to stream events back through MCP for v1.

Run with `uv run clawdaw-agent` (stdio transport).
"""

from __future__ import annotations

import logging
import os
import sys
from contextlib import asynccontextmanager
from typing import Any

import grpc
from google.protobuf import json_format
from google.protobuf.message import Message
from mcp.server.fastmcp import Context, FastMCP

from .engine_client import DEFAULT_TARGET, EngineClient

log = logging.getLogger(__name__)


def _to_dict(msg: Message) -> dict[str, Any]:
    """Protobuf → JSON-safe dict.

    `including_default_value_fields` so consumers don't have to guess
    whether a missing key means "false / 0" or "field genuinely absent."
    Enums come back as names rather than ints (e.g. `TRACK_TYPE_AUDIO`).
    """
    return json_format.MessageToDict(
        msg,
        always_print_fields_with_no_presence=True,
        preserving_proto_field_name=True,
        use_integers_for_enums=False,
    )


def _err(action: str, e: grpc.aio.AioRpcError) -> dict[str, Any]:
    """Render a gRPC failure as a structured tool result rather than raising;
    LLMs handle structured errors better than tracebacks."""
    return {
        "ok": False,
        "action": action,
        "grpc_code": e.code().name if e.code() else "UNKNOWN",
        "details": e.details() or "",
    }


@asynccontextmanager
async def engine_lifespan(_: FastMCP):
    """Open the gRPC channel and start the event buffer on server boot;
    close the channel on shutdown."""
    target = os.environ.get("CLAWDAW_ENGINE_ADDR", DEFAULT_TARGET)
    client = EngineClient(target=target)
    await client.connect()
    try:
        await client.start_event_stream()
    except Exception as exc:  # noqa: BLE001 — best-effort background subscriber
        log.warning("could not start event stream: %s", exc)
    try:
        yield {"engine": client}
    finally:
        await client.close()


mcp = FastMCP(
    name="clawdaw-agent",
    instructions=(
        "Tools for driving the CLAWDAW audio engine — list/inspect tracks, "
        "rename them, set volume/pan, add and tweak plugins, and undo any "
        "mutation by passing back the commit_id you got. The engine is the "
        "source of truth; round-trip a read after any mutation if you need "
        "to confirm state."
    ),
    lifespan=engine_lifespan,
)


def _client(ctx: Context) -> EngineClient:
    return ctx.request_context.lifespan_context["engine"]


# ============================================================================
# Reads
# ============================================================================


@mcp.tool()
async def list_tracks(ctx: Context) -> dict[str, Any]:
    """List every track in the open project — track id, name, type, color, mute/solo/arm flags.

    Use this first to discover track ids before calling any track-specific tool.
    """
    try:
        resp = await _client(ctx).list_tracks()
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("list_tracks", e)


@mcp.tool()
async def get_project(ctx: Context) -> dict[str, Any]:
    """Return the full open project: name, tempo_bpm, key, time signature, sample rate, and all tracks.

    Heavier than `list_tracks` — call this when you need project-level fields too.
    """
    try:
        resp = await _client(ctx).get_project()
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("get_project", e)


@mcp.tool()
async def get_track(ctx: Context, track_id: str | int) -> dict[str, Any]:
    """Return one track in full: regions, plugin chain (each with instance id),
    mixer state, color, type.

    Args:
        track_id: Engine-assigned track id. Strings or integers are both accepted; the engine uses strings.
    """
    try:
        resp = await _client(ctx).get_track(track_id)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("get_track", e)


# ============================================================================
# Track mutations
# ============================================================================


@mcp.tool()
async def add_track(
    ctx: Context,
    name: str = "",
    track_type: str = "TRACK_TYPE_AUDIO",
) -> dict[str, Any]:
    """Create a new track at the end of the project. Returns `track_id` and a `commit_id` for undo.

    Args:
        name: Display name for the new track. Empty falls back to the engine's default ("Audio N").
        track_type: One of `TRACK_TYPE_AUDIO`, `TRACK_TYPE_MIDI`, `TRACK_TYPE_BUS`, `TRACK_TYPE_FOLDER`,
            `TRACK_TYPE_AUX`. v1 supports `TRACK_TYPE_AUDIO` only; others return INVALID_ARGUMENT.
    """
    try:
        resp = await _client(ctx).add_track(track_type=track_type, name=name)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("add_track", e)


@mcp.tool()
async def delete_track(
    ctx: Context, track_id: str | int, confirm: bool = False
) -> dict[str, Any]:
    """Delete a track. DESTRUCTIVE — removes all regions and plugins on the track. Requires `confirm=True`.

    The deletion is undoable: pass the returned `commit_id` to `undo` to restore the
    track (with its plugins, regions, and mixer state). The engine refuses the call
    with INVALID_ARGUMENT if `confirm` is false, so a typo can't nuke a track.

    Args:
        track_id: Engine track id.
        confirm: MUST be True. False (the default) returns INVALID_ARGUMENT.
    """
    try:
        resp = await _client(ctx).delete_track(track_id, confirm=confirm)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("delete_track", e)


@mcp.tool()
async def rename_track(ctx: Context, track_id: str | int, name: str) -> dict[str, Any]:
    """Rename a track. Returns a `commit_id` you can pass to `undo` to revert.

    Args:
        track_id: Engine track id.
        name: New display name. Empty strings are accepted but discouraged.
    """
    try:
        resp = await _client(ctx).rename_track(track_id, name)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("rename_track", e)


@mcp.tool()
async def set_track_volume(
    ctx: Context, track_id: str | int, volume_db: float
) -> dict[str, Any]:
    """Set a track's fader in decibels relative to unity. Returns `commit_id` for undo.

    Args:
        track_id: Engine track id.
        volume_db: -inf..+12 dB. 0 dB is unity. The engine clamps but won't error.
    """
    try:
        resp = await _client(ctx).set_track_volume(track_id, volume_db)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("set_track_volume", e)


@mcp.tool()
async def set_track_pan(ctx: Context, track_id: str | int, pan: float) -> dict[str, Any]:
    """Set a track's pan. -1.0 = full left, 0.0 = center, +1.0 = full right. Returns `commit_id` for undo.

    Args:
        track_id: Engine track id.
        pan: -1.0 to +1.0.
    """
    try:
        resp = await _client(ctx).set_track_pan(track_id, pan)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("set_track_pan", e)


@mcp.tool()
async def undo(ctx: Context, commit_id: str = "") -> dict[str, Any]:
    """Undo the most recent mutation, or a specific one by commit_id.

    Args:
        commit_id: Optional. If empty, undo the most recent mutation. If set, undo that specific commit.
    """
    try:
        resp = await _client(ctx).undo(commit_id)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("undo", e)


# ============================================================================
# Transport
# ============================================================================


@mcp.tool()
async def play(ctx: Context) -> dict[str, Any]:
    """Start playback from the current transport position. Returns commit_id (pass to undo to stop).

    No-ops cleanly if playback is already running — the engine still returns a
    commit_id but undo of it does nothing in that case.
    """
    try:
        resp = await _client(ctx).play()
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("play", e)


@mcp.tool()
async def stop(ctx: Context) -> dict[str, Any]:
    """Stop playback. Returns commit_id (pass to undo to resume).

    No-ops cleanly if playback was already stopped.
    """
    try:
        resp = await _client(ctx).stop()
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("stop", e)


@mcp.tool()
async def get_transport_state(ctx: Context) -> dict[str, Any]:
    """Read the current transport state: is_playing, is_recording, position in seconds.

    Returns a dict with `playing`, `recording`, and `position.seconds`. Bars/beats
    aren't reported live yet — the engine leaves them at 0 until the tempo-sequence
    helper lands.
    """
    try:
        resp = await _client(ctx).get_transport_state()
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("get_transport_state", e)


# ============================================================================
# Plugins
# ============================================================================


@mcp.tool()
async def rescan_plugins(ctx: Context) -> dict[str, Any]:
    """Re-scan installed AU / VST3 plugins. Long-running on a cold cache (tens of seconds). Returns a `commit_id`.

    Usually only needed after the user installs new plugins; the engine caches results across launches.
    """
    try:
        resp = await _client(ctx).rescan_plugins()
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("rescan_plugins", e)


@mcp.tool()
async def list_available_plugins(ctx: Context) -> dict[str, Any]:
    """List every plugin the engine knows about — name, vendor, format, category, and the `id` to pass to `add_plugin`.

    Returns a long list (often hundreds of entries); filter client-side if you need a category.
    """
    try:
        resp = await _client(ctx).list_available_plugins()
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("list_available_plugins", e)


@mcp.tool()
async def add_plugin(
    ctx: Context,
    track_id: str | int,
    plugin_id: str,
    slot: int | None = None,
) -> dict[str, Any]:
    """Insert a plugin into a track's plugin chain. Returns `plugin_instance_id` and `commit_id`.

    Args:
        track_id: Engine track id.
        plugin_id: From `list_available_plugins` — the engine's identifier, not the display name.
        slot: Optional 0-based insertion index. Omit (or pass null) to append to the end.
    """
    try:
        resp = await _client(ctx).add_plugin(track_id, plugin_id, slot)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("add_plugin", e)


@mcp.tool()
async def get_plugin_parameters(ctx: Context, plugin_instance_id: str) -> dict[str, Any]:
    """List a plugin instance's parameters with current value, range, and normalized form.

    Args:
        plugin_instance_id: From `add_plugin` or `get_track().plugins[].instance_id`.
    """
    try:
        resp = await _client(ctx).get_plugin_parameters(plugin_instance_id)
        return _to_dict(resp)
    except grpc.aio.AioRpcError as e:
        return _err("get_plugin_parameters", e)


@mcp.tool()
async def set_plugin_parameter(
    ctx: Context,
    plugin_instance_id: str,
    param_id: str,
    normalized: float | None = None,
    native_value: float | None = None,
) -> dict[str, Any]:
    """Set one plugin parameter, by normalized 0..1 or by native value. Pass exactly one. Returns `commit_id`.

    Args:
        plugin_instance_id: From `add_plugin` or `get_track`.
        param_id: From `get_plugin_parameters`.
        normalized: 0.0..1.0 normalized value. Pass this OR `native_value`, not both.
        native_value: Value in the plugin's native units. Pass this OR `normalized`, not both.
    """
    try:
        resp = await _client(ctx).set_plugin_parameter(
            plugin_instance_id,
            param_id,
            normalized=normalized,
            native_value=native_value,
        )
        return _to_dict(resp)
    except ValueError as e:
        return {"ok": False, "action": "set_plugin_parameter", "error": str(e)}
    except grpc.aio.AioRpcError as e:
        return _err("set_plugin_parameter", e)


# ============================================================================
# Events
# ============================================================================


@mcp.tool()
async def subscribe_events(ctx: Context, n: int = 20) -> dict[str, Any]:
    """Return the last N events from the agent's rolling buffer (default 20, max ≈256).

    The agent subscribes to the engine's event stream in the background, so this is
    a snapshot — call it after a mutation to see what fired, or to catch up on what
    a human user did via the UI. This does NOT stream events back to MCP; v1 returns
    a bounded snapshot only.
    """
    client = _client(ctx)
    raw = client.recent_events(n=n)
    return {
        "count": len(raw),
        "events": [_to_dict(e) for e in raw],
    }


# ============================================================================
# Entry point
# ============================================================================


def main() -> None:
    logging.basicConfig(
        level=os.environ.get("CLAWDAW_LOG_LEVEL", "INFO"),
        stream=sys.stderr,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    log.info("clawdaw-agent starting, engine target=%s", os.environ.get("CLAWDAW_ENGINE_ADDR", DEFAULT_TARGET))
    mcp.run()


if __name__ == "__main__":
    main()
