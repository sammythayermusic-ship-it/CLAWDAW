// App layout: agent indicator + connection-status pill at the very top,
// transport pill across the top, TrackList rail on the left, PluginChain
// rail on the right. Grid template uses `space.*` tokens for gaps.

import { TransportBar } from "./panels/TransportBar";
import { TrackList } from "./panels/TrackList";
import { PluginChain } from "./panels/PluginChain";
import { useEngineStore } from "./state/store";
import { useEngineSync } from "./state/useEngineSync";
import "./App.css";

export function App() {
  useEngineSync();
  const connection = useEngineStore((s) => s.connection);
  const project = useEngineStore((s) => s.project);

  return (
    <main className="app-shell">
      <header className="app-titlebar">
        <div className="app-brand">
          <span className="app-agent-pebble" aria-hidden />
          <span className="app-brand-label">CLAWDAW</span>
        </div>
        <div className="app-connection">
          <span className={`app-conn-dot app-conn-dot--${connection.status}`} />
          <span className="app-conn-label">
            {connection.status === "connected"
              ? `connected · ${project?.name || "untitled"}`
              : connection.status === "connecting"
                ? "connecting…"
                : connection.status === "disconnected"
                  ? `disconnected: ${truncate(connection.reason, 40)}`
                  : "idle"}
          </span>
        </div>
      </header>

      <TransportBar />

      <section className="app-rails">
        <TrackList />
        <PluginChain />
      </section>
    </main>
  );
}

function truncate(s: string, n: number): string {
  return s.length <= n ? s : s.slice(0, n - 1) + "…";
}
