// Hero "slot machine" transport pill — the visual centerpiece.
// Tracks are visual-only for Phase 3 (no Play/Stop/Record RPC yet); buttons
// don't yet wire to the engine but feel right when hovered/pressed.
// Master meter and digit readouts are sourced where possible:
//   - tempoBpm comes from the project (live).
//   - The bar.beat.sub readout is frozen at 12.3.1 — no transport-position
//     RPC yet; will animate once we add SubscribeTransportPosition.
//   - Master meter rms_db pulled from the Master bus track if present in
//     trackDetails; otherwise renders at a calm idle level.

import { useEngineStore } from "../state/store";
import { PlayIcon, RecordIcon, StopIcon } from "./icons";
import "./TransportBar.css";

const STATIC_BARBEAT = "12.3.1"; // matches token-preview.html until we have a transport-position RPC
const STATIC_MASTER_DB = "−2.4";

export function TransportBar() {
  const tempoBpm = useEngineStore((s) => Math.round(s.project?.tempoBpm ?? 120));

  return (
    <div className="transport-bar">
      <div className="transport-pill">
        <div className="transport-buttons">
          <button className="transport-btn transport-btn--play" aria-label="Play">
            <PlayIcon size={18} />
          </button>
          <button className="transport-btn transport-btn--stop" aria-label="Stop">
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
