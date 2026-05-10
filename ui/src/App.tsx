// Hour 2 visible loop: render live track names from the engine. The
// real three-panel UI lands in Hour 3.

import { useEngineStore, selectVisibleTracks } from "./state/store";
import { useEngineSync } from "./state/useEngineSync";
import "./App.css";

export function App() {
  useEngineSync();
  const tracks = useEngineStore(selectVisibleTracks);
  const connection = useEngineStore((s) => s.connection);
  const project = useEngineStore((s) => s.project);

  return (
    <main className="app-shell">
      <header className="app-header">
        <h1 className="app-title">CLAWDAW</h1>
        <div className="app-connection">
          <span className={`app-conn-dot app-conn-dot--${connection.status}`} />
          <span className="app-conn-label">
            {connection.status === "connected"
              ? `connected · ${project?.name ?? "untitled"}`
              : connection.status === "connecting"
                ? "connecting…"
                : connection.status === "disconnected"
                  ? `disconnected: ${connection.reason}`
                  : "idle"}
          </span>
        </div>
      </header>
      <ul className="app-track-list">
        {tracks.length === 0 ? (
          <li className="app-track-empty">no tracks</li>
        ) : (
          tracks.map((t) => (
            <li key={t.id} className="app-track-row">
              <span className="app-track-name">{t.name}</span>
              <span className="app-track-meta">
                {t.trackType.toLowerCase()} · {t.pluginCount} plugin
                {t.pluginCount === 1 ? "" : "s"}
              </span>
            </li>
          ))
        )}
      </ul>
    </main>
  );
}
