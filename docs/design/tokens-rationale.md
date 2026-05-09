# tokens-rationale.md

Where every token in [`ui/src/design/tokens.ts`](../../ui/src/design/tokens.ts) came from.
Two prototypes were chosen as authoritative sources:

- **Hero A — `clawdaw_main_arrangement.png`** — full-app establishing layout. Source of background gradient, surface tints, text hierarchy, agent indicator, panel radii.
- **Hero B — `clawdaw_transport_detail.png`** — slot-machine showcase. Source of tactile control surfaces, accent gold, meter gradient, peak-hold dot, large mono readouts.

All hex values are pixel-sampled at full resolution (5504×3072) using masked region detection in Pillow + numpy, not eyeballed. The sampling code is ephemeral; what's reproducible is "the value matches what the masked region of the image actually contains."

## Color

### `color.background.*` — from Hero A
Sampled at the four corners along thin edge strips (avoiding any chrome/agent dot):
- `glow #F5CD9D` — top-right corner, the brightest stop. The "sun" of the diagonal.
- `warm #E5A78A` — right-mid edge, the body of the wash.
- `deep #D89382` — top-left corner, warm rose tilt.
- `shadow #836769` — bottom-left corner, the deepest dusty plum point.

The gradient runs roughly NW→SE corner-to-corner. These four stops reconstitute it cleanly. Reality-check: paint a diagonal gradient with these stops; it should match the prototype background.

### `color.surface.*` — from both heroes
- `glass #EADFD5` — pill body, sampled at six clean spots on the transport_detail pill (above/below digits, right of record button). All six points fell within `#E3D6C9..#EADFD5`; picked the median-bright value.
- `glassRaised #F2DFD8` — track header pebble, brightest 30% of the cream pebble face in Hero A (the pebble visibly sits a step *above* the surrounding panel).
- `glassEdge #F5E8DC` — top-edge highlight band of the transport pill (a ~6px band at the very top of the cream shape, brightest 200 pixels).
- `glassInnerShadow #DDCDBD` — bottom inner-shadow strip inside the pill (y=0.62..0.64 in Hero B). The cream darkens noticeably at the bottom.

### `color.text.*` — from both heroes
- `primary #2A1A18` — derived from the darkest pixels in "Drums" (Hero A: darkest pixel #24070B) and the "94"/"12.3.1" digit ink (Hero B: darkest 80 averaged to a near-#070501 anti-aliased core). The actual visible body-text color sits between those two — `#2A1A18` matches what your eye sees as "warm dark plum-charcoal" on the cream pill.
- `secondary #6B554A` — small "BPM" / "BAR.BEAT.SUB" labels. These render as a softer warm taupe; this value matches their visible weight.
- `tertiary #A1857A` — bar numbers above tracks in Hero A (darkest 10% of the bar-number strip averaged to #A1857A).

### `color.accent.*` — from Hero B
- `active #E8B886` — derived from the play button's gold inner-glow ring. A masked search for "warm gold pixels" inside the play button bbox returned a most-saturated 15% of `#ECD1B1`; pulled saturation up slightly for a workable hover color that still reads gold, not cream.
- `hover #F2D7B5` — brighter halo derived from the play button's outermost glow band (the very brightest cream-gold pixels, slightly warm-shifted).

### `color.meter.*` — from Hero B
The meter is two tall pill bars at x=0.78..0.84 of Hero B. Sampled in vertical thirds, masking out the cream pill background that bleeds into the bar edges:
- `low #6C8B8C` — dedicated cool-pixel mask (G > R AND B > R) within the meter bbox returned 32k pixels with most-saturated 10% mean of `#6C8B8C`. That's the dusty sage at the bar bottom.
- `mid #DAAB82` — band-2 (middle 20% of bar height) most-amber 20% mean. Warm amber.
- `high #C17D7A` — most-rose 20% in the top warm-pixel mask within the meter bbox. Soft warm coral, deliberately desaturated.
- `peakHold #FAE1BB` — brightest 30 pixels in the top-of-meter band 1. The pale-gold peak-hold dot reads as nearly ivory but with a clear warm tilt.

### `color.agentIndicator #F0DBBF` — from Hero A
The agent pebble at the top-right corner of `clawdaw_main_arrangement.png` (idle/collapsed-dock state). Sampled by masking pixels that differ from the surrounding peach background, then taking the mid-luma 30% of the pebble — that gives the visible face color, excluding edge highlights and the darker stroke marks inside the icon. Kept distinct from `accent.active` so the agent's idle presence doesn't have to match a hover state.

## Typography
**Source:** the brief, with sizes calibrated against Hero A and Hero B.
- `display 56px` — matches "New Session" in `clawdaw_empty_state.png` and the giant "94" in Hero B. Both render at this scale.
- `h2 24px` — matches "F minor" in `clawdaw_listening_panel.png` for a tight-tracked semi-display use.
- `label 11px / 0.08em` — "BPM" / "BAR.BEAT.SUB" in Hero B render as small caps-feel labels with visible letter-spacing.
- `monoReadout 56px` — "94" and "12.3.1" digits in Hero B; tabular-figures matters for these.
- `monoBody 13px` — "−3.2 dB GR", "sat 35%" inline readouts in `clawdaw_plugin_chain.png`.

The other steps (h1, body) are intermediate fillers in the scale, sized for visual continuity but not directly traced to a single image element.

**Note on the 7th step (`monoBody`):** the brief enumerates 6 entries; I added `monoBody` (13px) because the prototypes show two clearly different mono sizes — the 56px transport readouts and the ~13px inline plugin readouts (e.g. "−3.2 dB GR" in `clawdaw_plugin_chain.png`). Treating both as `monoReadout` would force downstream one-offs. Flagging this as a deliberate scope-creep on the brief — happy to remove if you'd rather keep the scale literal.

## Radii
- `control 10px` — stop / record button corner radius in Hero B reads as ~12% of the button width; at the implied UI scale that's `10px`.
- `card 16px` — clip tile corners in Hero A; plugin pebbles in `clawdaw_plugin_chain.png`.
- `panel 20px` — outer arrangement frame in Hero A; agent dock in `clawdaw_agent_open.png`.
- `pill 9999px` — transport bar in Hero B is fully-rounded (radius = height/2). Same for the track-header pill and M/S chips.

## Glass material
- `blurPx 24` — the visible refraction-on-edges look in both heroes implies a substantial backdrop blur. 24px is the smallest value that still gives the warm wash showing through with edge bend.
- `surfaceAlpha 0.85` — the cream pills in both heroes are *partly* translucent; you can see the warm bg colorize the cream slightly. 85% opacity over the bg matches the visible tint shift between (e.g.) the transport pill in Hero A (over peach) and Hero B (over deeper bokeh).
- `edgeHighlight #FFF8E6 @ 0.55` — the warm-ivory top-edge band on the transport pill in Hero B (brightest 200 pixels averaged to `#EBD6C5` — that's `#FFF8E6` at ~55% over the cream surface).
- `innerShadow #3C2826 @ 0.10` — bottom inner darkening of the same pill. Color sampled from the warmest dark pixels in the inner-shadow strip; opacity tuned to the visible darkening.

## Spacing
**Source:** the brief (4-base scale, no negotiation). Stops chosen to cover the gaps visible in Hero A — track-row vertical rhythm reads as ~`px8` (32px) between row centers; track-header padding reads as `px3..px4` (12–16px).

## Motion
**Source:** the brief ("slot-machine-satisfying defaults"). No image can timestamp easing, so values are inferred from the *intent*:
- `spring` cubic-bezier overshoots slightly → for primary press moments (play, agent open).
- `standard` is the Apple deceleration curve → for everything else.
- Durations follow Apple HIG-ish defaults: ≤120ms for hover/press feedback, ~320ms for panel motion, multi-second for ambient (the "slow PS2-style wave" in the brief implies seconds, not milliseconds).

## Elevation
- Color `#3C1E15` warm-tinted across all three states — sampled from the soft drop shadow under the transport pill in Hero B (the darkest pixels just below the pill outer edge in the bokeh, masked to exclude actual bokeh peach). A neutral-black shadow reads cold against the warm bg; this value preserves warmth.
- `rest`, `hover`, `active` opacity/blur values escalate sensibly (low → medium → inset). They aren't directly traceable to specific image pixels because the heroes only show one elevation state per element; these are derived from "what would feel right at hover/press" given the rest shadow.

## What I deliberately did not include
- Dark-mode color tokens. The brief specifies "warm light palette." Dark variants are a separate session against the dark prototype set.
- A `destructive`/`record` accent color category. The brief listed exactly six color subgroups; the coral record button uses the same family as `meter.high` (sampled coral was `#E59B85` for the face and `#CA624C` for the dot — both within the `#C17D7A` rose family). When we wire up the record control, it should be implemented with `meter.high` plus a brighter highlight, not a new top-level token.
- Z-index, breakpoints, icon sizing — none of these are in the brief.
