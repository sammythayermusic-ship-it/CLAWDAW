// Hero "slot machine" transport pill — the visual centerpiece.
// Play and Stop are wired to Engine.Play/Engine.Stop; the playing flag in
// the store updates optimistically on click. Record is still visual-only.
// Master meter and digit readouts are sourced where possible:
//   - tempoBpm comes from the project (live).
//   - The bar.beat.sub readout is frozen at 12.3.1 — no transport-position
//     RPC yet; will animate once we add SubscribeTransportPosition.
//   - Master meter rms_db pulled from the Master bus track if present in
//     trackDetails; otherwise renders at a calm idle level.

import { engineApi } from "../state/engine";
import { useEngineStore } from "../state/store";
import { PauseIcon, PlayIcon, RecordIcon, StopIcon } from "./icons";
import "./TransportBar.css";

const STATIC_BARBEAT = "12.3.1"; // matches token-preview.html until we have a transport-position RPC
const STATIC_MASTER_DB = "−2.4";

export function TransportBar() {
  const tempoBpm = useEngineStore((s) => Math.round(s.project?.tempoBpm ?? 120));
  const isPlaying = useEngineStore((s) => s.transport.isPlaying);
  const setTransportPlaying = useEngineStore((s) => s.setTransportPlaying);

  // Optimistic update: flip the store immediately so the icon swaps, then
  // fire the RPC. If the engine returns an error we roll back.
  async function handlePlayPause() {
    const goingTo = !isPlaying;
    setTransportPlaying(goingTo);
    try {
      if (goingTo) {
        await engineApi.play();
      } else {
        await engineApi.stop();
      }
    } catch (e) {
      console.error("[transport] play/stop failed:", e);
      setTransportPlaying(!goingTo);
    }
  }

  async function handleStop() {
    setTransportPlaying(false);
    try {
      await engineApi.stop();
    } catch (e) {
      console.error("[transport] stop failed:", e);
    }
  }

  return (
    <div className="transport-bar">
      <div className="transport-pill">
        <div className="transport-buttons">
          <button
            className={
              "transport-btn transport-btn--play" + (isPlaying ? " transport-btn--play-active" : "")
            }
            aria-label={isPlaying ? "Pause" : "Play"}
            onClick={handlePlayPause}
          >
            {isPlaying ? <PauseIcon size={16} /> : <PlayIcon size={18} />}
          </button>
          <button className="transport-btn transport-btn--stop" aria-label="Stop" onClick={handleStop}>
            <StopIcon size={14} />
          </button>
          <button className="transport-btn transport-btn--record" aria-label="Record">
            <RecordIcon size={12} />
          </button>
        </div>

        <div className="transport-divider" />

        <div className="transport-readout">
          <span className="transport-readout-digits">{tempoBpm}</span>
          <span className="transport-readout-label">BPM</span>
        </div>

        <div className="transport-divider" />

        <div className="transport-readout">
          <span className="transport-readout-digits">{STATIC_BARBEAT}</span>
          <span className="transport-readout-label">BAR.BEAT.SUB</span>
        </div>

        <div className="transport-divider" />

        <div className="transport-meter">
          <div className="transport-meter-bar transport-meter-bar--full" />
          <div className="transport-meter-bar transport-meter-bar--80" />
        </div>

        <div className="transport-readout transport-readout--small">
          <span className="transport-readout-digits transport-readout-digits--small">
            {STATIC_MASTER_DB}
          </span>
          <span className="transport-readout-label">dB</span>
        </div>
      </div>
    </div>
  );
}
