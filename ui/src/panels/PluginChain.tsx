// Right-rail plugin chain for the selected track. Reads from the store's
// trackDetails — useEnsureSelectedTrackDetail kicks off a GetTrack fetch
// when the cache is empty (e.g. just-selected track, or after the reducer
// invalidated it because a Plugin* event fired).

import { selectSelectedTrackDetail, useEngineStore } from "../state/store";
import { useEnsureSelectedTrackDetail } from "../state/useEngineSync";
import "./PluginChain.css";

export function PluginChain() {
  useEnsureSelectedTrackDetail();
  const detail = useEngineStore(selectSelectedTrackDetail);
  const selectedId = useEngineStore((s) => s.selectedTrackId);

  if (!selectedId) {
    return (
      <section className="plugin-chain">
        <div className="plugin-chain-empty">select a track</div>
      </section>
    );
  }

  if (!detail) {
    return (
      <section className="plugin-chain">
        <header className="plugin-chain-header">
          <h2 className="plugin-chain-title">plugin chain</h2>
          <span className="plugin-chain-count">loading…</span>
        </header>
      </section>
    );
  }

  return (
    <section className="plugin-chain">
      <header className="plugin-chain-header">
        <h2 className="plugin-chain-title">{detail.name}</h2>
        <span className="plugin-chain-count">
          {detail.plugins.length} plugin{detail.plugins.length === 1 ? "" : "s"}
        </span>
      </header>

      {detail.plugins.length === 0 ? (
        <div className="plugin-chain-empty">no plugins</div>
      ) : (
        <ol className="plugin-chain-list">
          {detail.plugins.map((p) => (
            <li
              key={p.id}
              className={
                "plugin-chain-row" +
                (p.bypassed ? " plugin-chain-row--bypassed" : "")
              }
            >
              <span className="plugin-chain-slot">{p.slot.toString().padStart(2, "0")}</span>
              <span className="plugin-chain-info">
                <span className="plugin-chain-name">{p.name}</span>
                <span className="plugin-chain-meta">
                  <span className="plugin-chain-vendor">{p.vendor || "unknown vendor"}</span>
                  <span className="plugin-chain-format">{p.format}</span>
                </span>
              </span>
              <span
                className={
                  "plugin-chain-state" +
                  (p.bypassed ? " plugin-chain-state--bypassed" : " plugin-chain-state--active")
                }
              >
                {p.bypassed ? "bypassed" : "active"}
              </span>
            </li>
          ))}
        </ol>
      )}
    </section>
  );
}
