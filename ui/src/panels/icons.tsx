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
