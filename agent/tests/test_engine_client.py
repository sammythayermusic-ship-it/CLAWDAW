"""Integration smoke test for `EngineClient`.

Requires a running `clawdaw_engine` on 127.0.0.1:50051. Marked
`@pytest.mark.integration` so it is skipped by default in any CI that
runs `pytest` without selecting the marker explicitly.

Run with:
    uv run pytest -m integration
"""

from __future__ import annotations

import asyncio

import grpc
import pytest

from clawdaw_agent.engine_client import EngineClient


async def _engine_up(target: str = "127.0.0.1:50051") -> bool:
    chan = grpc.aio.insecure_channel(target)
    try:
        await asyncio.wait_for(chan.channel_ready(), timeout=1.0)
        return True
    except (TimeoutError, grpc.RpcError):
        return False
    finally:
        await chan.close()


@pytest.fixture
async def client():
    if not await _engine_up():
        pytest.skip("clawdaw_engine not running on 127.0.0.1:50051")
    c = EngineClient()
    await c.connect()
    yield c
    await c.close()


@pytest.mark.integration
async def test_rename_track_round_trip_and_undo(client: EngineClient) -> None:
    """get_project → rename one track → confirm rename via second get_project → undo → confirm revert.

    Picks an AUDIO (1) or MIDI (2) track. UNSPECIFIED (0) tracks are
    Tracktion-internal (Marker/Tempo/Chord/Arranger) and BUS (4) is the
    Master, which the engine accepts the RPC on but doesn't actually rename
    — possibly intentional in Tracktion.
    """
    before = await client.get_project()
    AUDIO, MIDI = 1, 2
    renamable = [t for t in before.tracks if t.type in (AUDIO, MIDI) and t.name]
    assert renamable, "engine has no AUDIO/MIDI tracks; cannot exercise rename"

    target = renamable[0]
    original_name = target.name
    new_name = f"{original_name}__pytest_phase4"

    mutation = await client.rename_track(target.id, new_name)
    assert mutation.commit_id, "rename_track returned an empty commit_id"

    after = await client.get_project()
    by_id = {t.id: t for t in after.tracks}
    assert by_id[target.id].name == new_name, (
        f"expected track {target.id} to be {new_name!r} after rename, got "
        f"{by_id[target.id].name!r}"
    )

    undo_result = await client.undo()
    assert undo_result.commit_id, "undo returned an empty commit_id"

    reverted = await client.get_project()
    by_id = {t.id: t for t in reverted.tracks}
    assert by_id[target.id].name == original_name, (
        f"expected track {target.id} to revert to {original_name!r}, got "
        f"{by_id[target.id].name!r}"
    )
