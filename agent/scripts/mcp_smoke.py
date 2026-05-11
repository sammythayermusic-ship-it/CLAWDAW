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

            print("\n== subscribe_events n=5 ==")
            res = await session.call_tool("subscribe_events", {"n": 5})
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
