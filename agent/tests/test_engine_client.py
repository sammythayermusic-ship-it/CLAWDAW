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
async def test_transport_play_stop_round_trip(client: EngineClient) -> None:
    """play() → poll get_transport_state → stop() → poll again.

    Sleeps briefly between play and the poll so the transport has time to
    actually start (the engine's mutation returns once the call is dispatched,
    not once playback is audible).
    """
    initial = await client.get_transport_state()
    assert initial.playing is False, "engine started with playback already running"

    mutation = await client.play()
    assert mutation.commit_id, "play returned no commit_id"
    await asyncio.sleep(0.1)

    after_play = await client.get_transport_state()
    assert after_play.playing is True, (
        f"expected playing=True after play(), got {after_play!r}"
    )

    stop_mutation = await client.stop()
    assert stop_mutation.commit_id, "stop returned no commit_id"
    await asyncio.sleep(0.05)

    after_stop = await client.get_transport_state()
    assert after_stop.playing is False, (
        f"expected playing=False after stop(), got {after_stop!r}"
    )


@pytest.mark.integration
async def test_add_track_and_delete_round_trip_and_undo(client: EngineClient) -> None:
    """add_track('TestTrack') → get_project (confirm) → delete_track confirm=true →
    get_project (confirm gone) → undo (restore) → undo (re-delete the add).

    Also asserts that delete_track with confirm=False is rejected with
    INVALID_ARGUMENT — the destructive-op gate.
    """
    before = await client.get_project()
    before_ids = {t.id for t in before.tracks}

    # Create
    add_resp = await client.add_track(name="pytest_phase5_track")
    assert add_resp.track_id, "add_track returned no track_id"
    assert add_resp.mutation.commit_id, "add_track mutation has no commit_id"
    new_id = add_resp.track_id

    after_add = await client.get_project()
    new_ids = {t.id for t in after_add.tracks}
    assert new_id in new_ids - before_ids, (
        f"expected new track id {new_id!r} in project after add, didn't find it"
    )
    by_id = {t.id: t for t in after_add.tracks}
    assert by_id[new_id].name == "pytest_phase5_track"

    # confirm=False must fail
    with pytest.raises(grpc.aio.AioRpcError) as excinfo:
        await client.delete_track(new_id, confirm=False)
    assert excinfo.value.code() == grpc.StatusCode.INVALID_ARGUMENT

    # confirm=True deletes
    del_mut = await client.delete_track(new_id, confirm=True)
    assert del_mut.commit_id

    after_delete = await client.get_project()
    assert new_id not in {t.id for t in after_delete.tracks}

    # Undo delete restores the track
    await client.undo()
    after_restore = await client.get_project()
    by_id = {t.id: t for t in after_restore.tracks}
    assert new_id in by_id, "undo did not restore the deleted track"
    assert by_id[new_id].name == "pytest_phase5_track"

    # Undo add removes it cleanly so the test leaves the project as it found it
    await client.undo()
    after_undo_add = await client.get_project()
    assert new_id not in {t.id for t in after_undo_add.tracks}


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
