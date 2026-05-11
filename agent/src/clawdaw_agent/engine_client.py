"""Async gRPC client for the CLAWDAW audio engine.

One method per implemented RPC, plus a background event-stream task that
keeps a bounded deque of recent events so the MCP `subscribe_events` tool
can return a snapshot. This module is a thin pass-through; cleverness
belongs in Claude, not here.

Two timeout disciplines to keep straight:

* Unary RPCs run with a per-call `timeout` argument. Default 5s.
* The streaming SubscribeEvents call gets NO timeout — applying one would
  cancel an idle stream after the deadline. See the 2026-05-10 bug-3 entry
  in `docs/session-log.md` for the matching mistake on the Tauri side.
"""

from __future__ import annotations

import asyncio
import logging
import os
from collections import deque
from collections.abc import Iterable
from typing import Final

import grpc
from daw.v1 import engine_pb2, engine_pb2_grpc, events_pb2
from google.protobuf import empty_pb2
from google.protobuf.message import Message

DEFAULT_TARGET: Final[str] = "127.0.0.1:50051"
DEFAULT_UNARY_TIMEOUT_S: Final[float] = 5.0
DEFAULT_EVENT_BUFFER: Final[int] = 256

log = logging.getLogger(__name__)


def _coerce_id(value: object) -> str:
    """Engine track / plugin IDs are strings on the wire. Accept int forms
    from MCP callers (Claude is happy to pass `1003` as a number) and coerce
    to str."""
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    return str(value)


class EngineClient:
    """Async gRPC client. Construct, `await connect()`, use, `await close()`.

    Read `EngineClient(target=...)`'s `target` from the env var
    `CLAWDAW_ENGINE_ADDR` if the caller doesn't pass one.
    """

    def __init__(
        self,
        target: str | None = None,
        unary_timeout_s: float = DEFAULT_UNARY_TIMEOUT_S,
        event_buffer_size: int = DEFAULT_EVENT_BUFFER,
    ) -> None:
        self.target = target or os.environ.get("CLAWDAW_ENGINE_ADDR", DEFAULT_TARGET)
        self.unary_timeout_s = unary_timeout_s
        self._channel: grpc.aio.Channel | None = None
        self._stub: engine_pb2_grpc.EngineStub | None = None
        self._recent_events: deque[events_pb2.Event] = deque(maxlen=event_buffer_size)
        self._event_task: asyncio.Task[None] | None = None

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    async def connect(self) -> None:
        if self._channel is not None:
            return
        log.info("connecting to clawdaw engine at %s", self.target)
        self._channel = grpc.aio.insecure_channel(self.target)
        self._stub = engine_pb2_grpc.EngineStub(self._channel)

    async def close(self) -> None:
        if self._event_task is not None:
            self._event_task.cancel()
            try:
                await self._event_task
            except (asyncio.CancelledError, Exception):
                pass
            self._event_task = None
        if self._channel is not None:
            await self._channel.close()
            self._channel = None
            self._stub = None

    def _ensure_stub(self) -> engine_pb2_grpc.EngineStub:
        if self._stub is None:
            raise RuntimeError("EngineClient.connect() has not been called")
        return self._stub

    # ------------------------------------------------------------------
    # Reads
    # ------------------------------------------------------------------

    async def get_project(self):
        return await self._ensure_stub().GetProject(
            empty_pb2.Empty(), timeout=self.unary_timeout_s
        )

    async def list_tracks(self):
        return await self._ensure_stub().ListTracks(
            empty_pb2.Empty(), timeout=self.unary_timeout_s
        )

    async def get_track(self, track_id: str | int):
        req = engine_pb2.GetTrackRequest(track_id=_coerce_id(track_id))
        return await self._ensure_stub().GetTrack(req, timeout=self.unary_timeout_s)

    # ------------------------------------------------------------------
    # Track mutations
    # ------------------------------------------------------------------

    async def rename_track(self, track_id: str | int, name: str):
        req = engine_pb2.RenameTrackRequest(track_id=_coerce_id(track_id), name=name)
        return await self._ensure_stub().RenameTrack(req, timeout=self.unary_timeout_s)

    async def set_track_volume(self, track_id: str | int, volume_db: float):
        req = engine_pb2.SetTrackVolumeRequest(
            track_id=_coerce_id(track_id), volume_db=float(volume_db)
        )
        return await self._ensure_stub().SetTrackVolume(req, timeout=self.unary_timeout_s)

    async def set_track_pan(self, track_id: str | int, pan: float):
        req = engine_pb2.SetTrackPanRequest(track_id=_coerce_id(track_id), pan=float(pan))
        return await self._ensure_stub().SetTrackPan(req, timeout=self.unary_timeout_s)

    async def undo(self, commit_id: str = ""):
        req = engine_pb2.UndoRequest(commit_id=commit_id or "")
        return await self._ensure_stub().Undo(req, timeout=self.unary_timeout_s)

    # ------------------------------------------------------------------
    # Plugins
    # ------------------------------------------------------------------

    async def rescan_plugins(self, *, timeout_s: float | None = None):
        # Plugin scans can take a while on a cold cache. Allow caller-side override.
        return await self._ensure_stub().RescanPlugins(
            empty_pb2.Empty(), timeout=timeout_s or max(self.unary_timeout_s, 30.0)
        )

    async def list_available_plugins(self):
        return await self._ensure_stub().ListAvailablePlugins(
            empty_pb2.Empty(), timeout=self.unary_timeout_s
        )

    async def add_plugin(
        self,
        track_id: str | int,
        plugin_id: str,
        slot: int | None = None,
    ):
        req = engine_pb2.AddPluginRequest(
            track_id=_coerce_id(track_id),
            plugin_id=plugin_id,
        )
        if slot is not None:
            req.slot = int(slot)
        return await self._ensure_stub().AddPlugin(req, timeout=self.unary_timeout_s)

    async def get_plugin_parameters(self, plugin_instance_id: str):
        req = engine_pb2.GetPluginParametersRequest(plugin_instance_id=plugin_instance_id)
        return await self._ensure_stub().GetPluginParameters(req, timeout=self.unary_timeout_s)

    async def set_plugin_parameter(
        self,
        plugin_instance_id: str,
        param_id: str,
        *,
        normalized: float | None = None,
        native_value: float | None = None,
    ):
        if (normalized is None) == (native_value is None):
            raise ValueError(
                "set_plugin_parameter requires exactly one of `normalized` or `native_value`"
            )
        req = engine_pb2.SetPluginParameterRequest(
            plugin_instance_id=plugin_instance_id,
            param_id=param_id,
        )
        if normalized is not None:
            req.normalized = float(normalized)
        else:
            req.native_value = float(native_value)
        return await self._ensure_stub().SetPluginParameter(req, timeout=self.unary_timeout_s)

    # ------------------------------------------------------------------
    # Events: background subscriber + bounded deque snapshot
    # ------------------------------------------------------------------

    async def start_event_stream(
        self,
        event_types: Iterable[str] | None = None,
        track_ids: Iterable[str | int] | None = None,
    ) -> None:
        """Spawn the background subscriber if it isn't already running."""
        if self._event_task is not None and not self._event_task.done():
            return
        self._event_task = asyncio.create_task(
            self._run_event_stream(
                list(event_types or []),
                [_coerce_id(t) for t in (track_ids or [])],
            )
        )

    async def _run_event_stream(self, event_types: list[str], track_ids: list[str]) -> None:
        stub = self._ensure_stub()
        req = events_pb2.EventFilter(event_types=event_types, track_ids=track_ids)
        # No timeout here — server-streaming RPCs must outlive the unary deadline.
        try:
            call = stub.SubscribeEvents(req)
            async for event in call:
                self._recent_events.append(event)
        except asyncio.CancelledError:
            raise
        except grpc.aio.AioRpcError as e:
            log.warning("SubscribeEvents stream ended: %s %s", e.code(), e.details())

    def recent_events(self, n: int = 50) -> list[events_pb2.Event]:
        if n <= 0:
            return []
        if n >= len(self._recent_events):
            return list(self._recent_events)
        return list(self._recent_events)[-n:]


__all__ = [
    "EngineClient",
    "DEFAULT_TARGET",
    "DEFAULT_UNARY_TIMEOUT_S",
    "Message",
]
