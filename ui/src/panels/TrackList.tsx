// Left-rail track list. One row per visible track. Selecting a row updates
// the store's selectedTrackId; downstream PluginChain reads that id to
// render its plugin chain.

import { useEngineStore, selectVisibleTracks } from "../state/store";
import "./TrackList.css";

export function TrackList() {
  const tracks = useEngineStore(selectVisibleTracks);
  const selectedId = useEngineStore((s) => s.selectedTrackId);
  const setSelected = useEngineStore((s) => s.setSelectedTrack);
  const trackDetails = useEngineStore((s) => s.trackDetails);
  const project = useEngineStore((s) => s.project);

  return (
    <aside className="track-list">
      <header className="track-list-header">
        <h2 className="track-list-title">{project?.name || "untitled"}</h2>
        <span className="track-list-count">
          {tracks.length} track{tracks.length === 1 ? "" : "s"}
        </span>
      </header>

      <ul className="track-list-rows">
        {tracks.length === 0 ? (
          <li className="track-list-empty">no tracks yet</li>
        ) : (
          tracks.map((t) => {
            const detail = trackDetails[t.id];
            const volumeDb = detail?.mixer?.volumeDb;
            return (
              <li key={t.id}>
                <button
                  className={
                    "track-list-row" +
                    (selectedId === t.id ? " track-list-row--selected" : "")
                  }
                  type="button"
                  onClick={() => setSelected(t.id)}
                >
                  <span className="track-list-row-name">{t.name}</span>
                  <span className="track-list-row-meta">
                    <span className="track-list-row-type">{trackTypeLabel(t.trackType)}</span>
                    <span className="track-list-row-volume">{formatDb(volumeDb)}</span>
                  </span>
                </button>
              </li>
            );
          })
        )}
      </ul>
    </aside>
  );
}

function trackTypeLabel(t: string): string {
  switch (t) {
    case "AUDIO":
      return "audio";
    case "MIDI":
      return "midi";
    case "BUS":
      return "bus";
    case "AUX":
      return "aux";
    case "FOLDER":
      return "folder";
    default:
      return "—";
  }
}

function formatDb(db: number | undefined): string {
  if (db == null) return "— dB";
  // Sub-dB precision is noise; round to 1 decimal. Use unicode minus for typography.
  if (Math.abs(db) < 0.05) return "0.0 dB";
  const sign = db < 0 ? "−" : "+";
  return `${sign}${Math.abs(db).toFixed(1)} dB`;
}
