// Left-rail track list. One row per visible track. Selecting a row updates
// the store's selectedTrackId; downstream PluginChain reads that id to
// render its plugin chain.
//
// The "+" button at the bottom calls engine_add_track; the trash icon on
// hover of each row calls engine_delete_track(confirm=true). Both round-
// trip through the engine; the UI then mirrors the resulting events.

import { useShallow } from "zustand/shallow";

import { engineApi } from "../state/engine";
import { useEngineStore, selectVisibleTracks } from "../state/store";
import { PlusIcon, TrashIcon } from "./icons";
import "./TrackList.css";

export function TrackList() {
  // Zustand v5 uses Object.is for equality on selector output. selectVisibleTracks
  // returns a new array reference each call (via .map().filter()), so the bare
  // selector triggers an infinite re-render loop. useShallow does a shallow
  // (per-element) compare of the result and breaks the loop.
  const tracks = useEngineStore(useShallow(selectVisibleTracks));
  const selectedId = useEngineStore((s) => s.selectedTrackId);
  const setSelected = useEngineStore((s) => s.setSelectedTrack);
  const trackDetails = useEngineStore((s) => s.trackDetails);
  const project = useEngineStore((s) => s.project);

  async function handleAdd() {
    const audioCount =
      tracks.filter((t) => t.trackType === "AUDIO").length + 1;
    const name = `Audio ${audioCount}`;
    try {
      await engineApi.addTrack("AUDIO", name);
      // Selection happens in useEngineSync after the TrackAdded event drives
      // a project refetch; no need to setSelected here.
    } catch (err) {
      console.error("addTrack failed:", err);
    }
  }

  async function handleDelete(trackId: string) {
    try {
      await engineApi.deleteTrack(trackId);
    } catch (err) {
      console.error("deleteTrack failed:", err);
    }
  }

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
            const isMaster = t.trackType === "BUS";
            return (
              <li key={t.id} className="track-list-row-wrap">
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
                {!isMaster && (
                  <button
                    className="track-list-row-delete"
                    type="button"
                    aria-label={`Delete track ${t.name}`}
                    onClick={(e) => {
                      e.stopPropagation();
                      void handleDelete(t.id);
                    }}
                  >
                    <TrashIcon size={14} />
                  </button>
                )}
              </li>
            );
          })
        )}
      </ul>

      <button
        className="track-list-add"
        type="button"
        onClick={() => void handleAdd()}
      >
        <PlusIcon size={14} />
        <span>add audio track</span>
      </button>
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
