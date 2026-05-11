// Tiny inline SVGs for transport buttons. Sized via `width`/`height` props
// or font-size + 1em. Stroke/fill use currentColor so callers control color
// via CSS.

export function PlayIcon({ size = 18 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 20 20" fill="currentColor" aria-hidden>
      <path d="M6 4 L16 10 L6 16 Z" />
    </svg>
  );
}

export function PauseIcon({ size = 16 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 16 16" fill="currentColor" aria-hidden>
      <rect x="3" y="3" width="3.5" height="10" rx="1" />
      <rect x="9.5" y="3" width="3.5" height="10" rx="1" />
    </svg>
  );
}

export function StopIcon({ size = 14 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 14 14" fill="currentColor" aria-hidden>
      <rect x="2" y="2" width="10" height="10" rx="2" />
    </svg>
  );
}

export function RecordIcon({ size = 14 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 14 14" fill="currentColor" aria-hidden>
      <circle cx="7" cy="7" r="4" />
    </svg>
  );
}

export function ChevronIcon({ size = 12 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.5" aria-hidden>
      <path d="M4 3 L8 6 L4 9" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

export function PlusIcon({ size = 14 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.5" aria-hidden>
      <path d="M7 2 V12 M2 7 H12" strokeLinecap="round" />
    </svg>
  );
}

export function TrashIcon({ size = 14 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.4" aria-hidden>
      <path d="M2.5 3.5 H11.5" strokeLinecap="round" />
      <path d="M5.5 3.5 V2.5 A0.5 0.5 0 0 1 6 2 H8 A0.5 0.5 0 0 1 8.5 2.5 V3.5" strokeLinecap="round" strokeLinejoin="round" />
      <path d="M3.5 3.5 L4 11.5 A0.5 0.5 0 0 0 4.5 12 H9.5 A0.5 0.5 0 0 0 10 11.5 L10.5 3.5" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}
