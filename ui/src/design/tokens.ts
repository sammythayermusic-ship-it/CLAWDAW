// CLAWDAW — Phase 3 design tokens, warm light palette.
// Sources: docs/design/prototypes/clawdaw_main_arrangement.png (background, surfaces,
// text, agent indicator) + clawdaw_transport_detail.png (controls, meter, accents).
// All hex values were sampled from those PNGs at full resolution; provenance notes
// live in docs/design/tokens-rationale.md.

// ---------------------------------------------------------------------------
// Color
//   Warm-light only. Dark variants are a separate pass — do not add them here.
// ---------------------------------------------------------------------------
export const color = {
  // Background — diagonal gradient warming from deep dusty plum (bottom-left)
  // to peach-gold (top-right). Use as a single backdrop behind every panel.
  background: {
    glow:   "#F5CD9D", // top-right "sun" stop — warmest, lightest
    warm:   "#E5A78A", // mid-right peach — main body of the wash
    deep:   "#D89382", // top-left warm rose
    shadow: "#836769", // bottom-left dusty plum — deepest stop
  },

  // Surfaces — warm cream/ivory glass. Combined with `glass.*` for blur + alpha.
  // The two cream tones differ by ~5% lightness; pick `raised` for pebbles
  // sitting visibly above other glass (track headers, plugin pebbles).
  surface: {
    glass:        "#EADFD5", // canonical pill / panel cream
    glassRaised:  "#F2DFD8", // raised pebbles (track header, button face)
    glassEdge:    "#F5E8DC", // top-edge highlight cream (subtle brighter band)
    glassInnerShadow: "#DDCDBD", // bottom inner-shadow tint inside cream pill
  },

  // Text — warm dark plum-charcoal at the top, fading to taupe.
  text: {
    primary:   "#2A1A18", // body ink: track names, big readouts, control glyphs
    secondary: "#6B554A", // small labels: "BPM", "BAR.BEAT.SUB", muted UI text
    tertiary:  "#A1857A", // bar numbers, M/S badges, decorative ruler text
  },

  // Accents — warm gold for primary affordance (play, hover halo).
  // The agent indicator is its own token (see below) so the agent presence
  // can drift independently from interactive accents.
  accent: {
    active: "#E8B886", // hovered/pressed primary — play glow, focus ring
    hover:  "#F2D7B5", // brighter halo on hover (sits over `active` as a glow)
  },

  // Master / track meter gradient. Brief uses "green/amber/red" naming; we
  // honor the *role* (low/mid/high) but lean dusty teal → amber → coral so
  // the meter never reads as a sterile LED row.
  meter: {
    low:  "#6C8B8C", // dusty sage/teal — quiet signal at bar bottom
    mid:  "#DAAB82", // warm amber — nominal
    high: "#C17D7A", // warm coral/rose — approaching peak
    peakHold: "#FAE1BB", // pale gold dot — peak-hold marker above the bar
  },

  // Agent indicator — soft warm cream-gold pebble. Used wherever the agent
  // is *present* (idle pebble, dock header chip). Distinct from `accent.active`
  // so the agent can be visible without competing with primary actions.
  agentIndicator: "#F0DBBF",
} as const;

// ---------------------------------------------------------------------------
// Typography
//   System fonts only. SF Pro Display for >= 28px headings, SF Pro Text for
//   body and labels, SF Mono for any numerical readout (tabular figures).
// ---------------------------------------------------------------------------
export const typography = {
  family: {
    display: "'SF Pro Display', -apple-system, BlinkMacSystemFont, sans-serif",
    text:    "'SF Pro Text', -apple-system, BlinkMacSystemFont, sans-serif",
    mono:    "'SF Mono', ui-monospace, Menlo, monospace",
  },
  // Type scale. {size, lineHeight, weight, tracking} — tracking in em.
  scale: {
    display:     { size: 56, lineHeight: 1.05, weight: 600, tracking: -0.022 }, // empty-state hero
    h1:          { size: 36, lineHeight: 1.15, weight: 600, tracking: -0.018 }, // section heads
    h2:          { size: 24, lineHeight: 1.22, weight: 500, tracking: -0.012 }, // panel titles, "F minor"
    body:        { size: 15, lineHeight: 1.45, weight: 400, tracking:  0     }, // agent dialogue, descriptions
    label:       { size: 11, lineHeight: 1.30, weight: 500, tracking:  0.080 }, // "BPM", "BAR.BEAT.SUB" — small caps feel
    monoReadout: { size: 56, lineHeight: 1.00, weight: 500, tracking: -0.005 }, // "94", "12.3.1" big digits
    monoBody:    { size: 13, lineHeight: 1.35, weight: 400, tracking:  0     }, // "-3.2 dB GR", inline readouts
  },
} as const;

// ---------------------------------------------------------------------------
// Radii — rounded square family. Pill = fully rounded.
// ---------------------------------------------------------------------------
export const radii = {
  control: 10, // play/stop/record button face, slider thumb, plugin pebble
  card:    16, // clip tile, plugin card, agent message bubble
  panel:   20, // arrangement frame, agent dock, listening panel
  pill:    9999, // transport pill, M/S chip — fully rounded
} as const;

// ---------------------------------------------------------------------------
// Glass material — Tahoe-style liquid glass. Apply as a layered effect:
//   1) backdrop-filter: blur(${blurPx}px) saturate(1.05)
//   2) background-color: surface.* + alpha
//   3) box-shadow: edgeHighlight inset (top) + innerShadow inset (bottom)
// ---------------------------------------------------------------------------
export const glass = {
  blurPx: 24,            // backdrop blur radius — visible refraction at panel edges
  surfaceAlpha: 0.85,    // cream surface opacity over warm background
  edgeHighlight: {
    color:   "#FFF8E6",  // warm ivory top-edge highlight — slightly warmer than white
    opacity: 0.55,       // subtle band at the very top of any pill/panel
  },
  innerShadow: {
    // Inset bottom shadow — gives glass its "weight" without a hard outline.
    offsetY: 1,
    blur:    2,
    color:   "#3C2826",
    opacity: 0.10,
  },
} as const;

// ---------------------------------------------------------------------------
// Spacing — 4-px base scale.
// ---------------------------------------------------------------------------
export const space = {
  px0:  0,
  px1:  4,
  px2:  8,
  px3:  12,
  px4:  16,
  px5:  20,
  px6:  24,
  px8:  32,
  px10: 40,
  px12: 48,
  px16: 64,
} as const;

// ---------------------------------------------------------------------------
// Motion — durations + easings tuned for "polished pebble" tactile feel.
//   `spring` overshoots slightly; reserve it for primary press/release moments
//   where a small bounce is the point (play button, agent open, slot-machine
//   meter snaps). `standard` is the Apple-style smooth deceleration default.
// ---------------------------------------------------------------------------
export const motion = {
  duration: {
    hover:      120,  // ms — color/glow swap on hover
    press:       80,  // ms — depth shift on press
    panelSlide: 320,  // ms — agent dock open/close, plugin window slide-in
    ambient:   1600,  // ms — slow background wave breathing
  },
  easing: {
    standard: "cubic-bezier(0.32, 0.72, 0.00, 1.00)",  // smooth deceleration
    spring:   "cubic-bezier(0.50, 1.60, 0.40, 0.95)",  // slight overshoot — tactile snap
    gentle:   "cubic-bezier(0.40, 0.00, 0.20, 1.00)",  // material default for non-primary motion
  },
} as const;

// ---------------------------------------------------------------------------
// Elevation — drop shadows for floating glass. Color is warm-tinted, not
// neutral black; otherwise shadows read cold against the peach background.
// ---------------------------------------------------------------------------
export const elevation = {
  // Sitting on the background (e.g. a track header pebble at rest)
  rest: {
    offsetY: 2,
    blur:    8,
    color:   "#3C1E15",
    opacity: 0.08,
  },
  // Lifted on hover (e.g. a hovered plugin pebble)
  hover: {
    offsetY: 6,
    blur:    18,
    color:   "#3C1E15",
    opacity: 0.12,
  },
  // Pressed-in (slight inset, less drop)
  active: {
    offsetY: 1,
    blur:    3,
    color:   "#3C1E15",
    opacity: 0.10,
    inset:   true,
  },
} as const;

// ---------------------------------------------------------------------------
// Aggregate export — a single namespace import is convenient in components:
//   import { tokens } from "@/design/tokens";
// ---------------------------------------------------------------------------
export const tokens = { color, typography, radii, glass, space, motion, elevation } as const;
export type Tokens = typeof tokens;
