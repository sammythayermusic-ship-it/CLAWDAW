"""Manual end-to-end smoke probe over the MCP stdio transport.

Boots the server as a subprocess (`uv run clawdaw-agent`), connects as an
MCP client, lists tools, then exercises rename_track → get_project → undo →
get_project. Prints the trace so we can paste it into session-log.md.

Run from agent/:
    uv run python scripts/mcp_smoke.py
"""

from __future__ import annotations

import asyncio
import json
import sys

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client


def _payload(result):
    # Tool result -> dict via the structured content if present, otherwise the
    # first text content parsed as JSON.
    if getattr(result, "structuredContent", None):
        return result.structuredContent
    for c in result.content:
        if c.type == "text":
            try:
                return json.loads(c.text)
            except json.JSONDecodeError:
                return {"raw": c.text}
    return {}


async def main() -> int:
    params = StdioServerParameters(command="uv", args=["run", "clawdaw-agent"])
    async with stdio_client(params) as (r, w):
        async with ClientSession(r, w) as session:
            await session.initialize()

            tools = await session.list_tools()
            print(f"== tools: {len(tools.tools)} registered ==")
            for t in tools.tools:
                print(f"  - {t.name}")

            print("\n== get_project ==")
            res = await session.call_tool("get_project", {})
            proj = _payload(res)
            AUDIO, MIDI = "TRACK_TYPE_AUDIO", "TRACK_TYPE_MIDI"
            tracks = proj.get("tracks", [])
            renamable = [t for t in tracks if t.get("type") in (AUDIO, MIDI)]
            if not renamable:
                print("FATAL: no renamable tracks in project; aborting")
                return 2
            target = renamable[0]
            target_id = target["id"]
            before_name = target.get("name", "")
            print(f"  target track: id={target_id!r} name={before_name!r}")

            new_name = f"{before_name}__mcp_smoke"
            print(f"\n== rename_track id={target_id!r} → {new_name!r} ==")
            res = await session.call_tool(
                "rename_track", {"track_id": target_id, "name": new_name}
            )
            mut = _payload(res)
            commit_id = mut.get("commit_id", "")
            print(f"  commit_id={commit_id!r} description={mut.get('description', '')!r}")
            if not commit_id:
                print("FATAL: rename did not return a commit_id")
                return 3

            print("\n== get_project (confirm rename) ==")
            res = await session.call_tool("get_project", {})
            proj = _payload(res)
            now = next(t for t in proj["tracks"] if t["id"] == target_id)
            print(f"  track {target_id!r} name now: {now.get('name', '')!r}")
            assert now["name"] == new_name, f"expected {new_name!r}, got {now['name']!r}"

            print(f"\n== undo commit_id={commit_id!r} ==")
            res = await session.call_tool("undo", {"commit_id": commit_id})
            undo_mut = _payload(res)
            print(
                f"  commit_id={undo_mut.get('commit_id', '')!r}"
                f" description={undo_mut.get('description', '')!r}"
            )

            print("\n== get_project (confirm revert) ==")
            res = await session.call_tool("get_project", {})
            proj = _payload(res)
            reverted = next(t for t in proj["tracks"] if t["id"] == target_id)
            print(f"  track {target_id!r} name now: {reverted.get('name', '')!r}")
            assert reverted["name"] == before_name, (
                f"expected revert to {before_name!r}, got {reverted['name']!r}"
            )

            # ---- transport + add/delete demo: the "agent runs the DAW" path ----
            print("\n== add_track name='Drums' ==")
            res = await session.call_tool(
                "add_track", {"name": "Drums", "track_type": "TRACK_TYPE_AUDIO"}
            )
            add_payload = _payload(res)
            new_track_id = add_payload.get("track_id", "")
            print(f"  new track id={new_track_id!r}")
            print(f"  commit_id={add_payload.get('mutation', {}).get('commit_id', '')!r}")
            assert new_track_id, "add_track returned no track_id"

            print("\n== get_project (confirm Drums present) ==")
            res = await session.call_tool("get_project", {})
            proj_after_add = _payload(res)
            drums = next(
                (t for t in proj_after_add["tracks"] if t["id"] == new_track_id), None
            )
            assert drums is not None and drums.get("name") == "Drums"
            print(f"  Drums confirmed: id={new_track_id!r} name='Drums'")

            print("\n== play ==")
            play_payload = _payload(await session.call_tool("play", {}))
            print(f"  description={play_payload.get('description', '')!r}")

            await asyncio.sleep(0.1)
            print("\n== get_transport_state ==")
            ts = _payload(await session.call_tool("get_transport_state", {}))
            print(f"  playing={ts.get('playing')} position.sec={ts.get('position', {}).get('seconds')}")
            assert ts.get("playing") is True

            print("\n== stop ==")
            stop_payload = _payload(await session.call_tool("stop", {}))
            print(f"  description={stop_payload.get('description', '')!r}")

            print("\n== delete_track confirm=false (should fail) ==")
            failed = _payload(
                await session.call_tool(
                    "delete_track", {"track_id": new_track_id, "confirm": False}
                )
            )
            print(f"  ok={failed.get('ok')} grpc_code={failed.get('grpc_code')}")
            assert failed.get("ok") is False
            assert failed.get("grpc_code") == "INVALID_ARGUMENT"

            # Cleanup: rewind transport + add via the undo log only. We avoid
            # exercising delete-then-undo in the smoke because of the
            # Tracktion track-cache quirk noted in the session log — the
            # pytest integration test covers that path in isolation.
            print("\n== 3 × undo (rewind: stop → play → add) ==")
            for i in range(3):
                resp = _payload(await session.call_tool("undo", {}))
                print(f"  {i + 1}. {resp.get('description', '')!r}")

            print("\n== get_project (verify clean) ==")
            final_proj = _payload(await session.call_tool("get_project", {}))
            still_drums = [t for t in final_proj["tracks"] if t["id"] == new_track_id]
            if still_drums:
                # Surface the diagnostic info before failing — the events buffer
                # below tells us which IDs the engine actually emitted.
                print(f"  WARNING: track {new_track_id!r} still present: {still_drums!r}")
            else:
                print(f"  Drums (id={new_track_id!r}) confirmed gone.")

            print("\n== subscribe_events n=20 ==")
            res = await session.call_tool("subscribe_events", {"n": 20})
            evbuf = _payload(res)
            print(f"  buffered events: {evbuf.get('count', 0)}")
            for e in evbuf.get("events", []):
                # The oneof field is on each event under whichever key fired.
                kinds = [k for k in e if k not in ("event_id", "timestamp_unix_ms")]
                print(f"    - {','.join(kinds)} ({e.get('event_id', '')[:8]})")

            print("\nOK")
            return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
