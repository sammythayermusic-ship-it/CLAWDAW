# CLAWDAW — Visual Prototype Generation

A plan and prompt pack for Claude Code to generate a set of UI prototype images for CLAWDAW using the Higgsfield MCP, then optionally hand them to Figma for organization.

Audience: future Claude Code session. Skim `CLAUDE.md` first if you haven't, then come back here.

---

## What we're going for

CLAWDAW is an agent-native DAW (think Logic / Ableton, but with a built-in audio engineer agent). We are NOT building these images for the engine — we already have an audio engine and gRPC layer. These images are mood-board / direction-setting prototypes to align on the look of the upcoming Phase 3 UI before we write a line of Tauri.

### Aesthetic anchors (these are the brief — internalize them)

1. **macOS Tahoe (macOS 26) liquid glass.** Translucent panels with real refraction at the edges, soft inner highlights, depth via blur and parallax — not flat material. System fonts: SF Pro / SF Mono. Generous corner radii (16–24px on panels, 8–12px on controls). Controls feel like polished pebbles sitting on glass.
2. **PS2 nostalgia warmth.** Early-2000s console-menu feel: gentle radial gradients, soft ambient glow, slow-moving wave-like backgrounds, a hint of chromatic shimmer. Color temperature warm — peach, amber, soft magenta, dusty teal — never sterile gray. Think the PS2 boot screen and Memory Card menu, not Y2K Frutiger Aero.
3. **Slot-machine satisfaction.** Composition should feel rewarding to look at: clear focal hierarchy, tactile primary controls, satisfying numerical readouts (BPM, dB, time), micro-rewards implied (ready-to-bounce play button, pulse on the meter). Without going skeuomorphic, every interactive element should look like it would feel good to click.
4. **Simple, not overbuilt.** Lots of negative space. The screen should breathe. If a feature isn't on the roadmap, it shouldn't be in the mockup. No fake spectrograms, no sixteen-band EQ on every track, no fictional plugin walls.

### Hard constraints / what NOT to do

- No Logic-Pro-style chrome (no metal, no inset bevels).
- No Ableton-style flat brutalism — we want warmth.
- No skeuomorphic knobs trying to look like 1970s hardware.
- No dark-mode-only thinking. Default these mockups to a *warm light* mode; we'll do dark variants in a second pass.
- No fake AI sparkles or generic "AI gradient" purple-blue. The agent presence should feel calm, not flashy.

---

## The shots to generate

Eight images, in this order. Each one is a separate Higgsfield generation. Aim for 16:10 or 16:9, ~2560px wide.

| # | Name | Purpose |
|---|------|---------|
| 1 | `clawdaw_main_arrangement` | Hero shot. Main arrangement view with 6–8 tracks, transport bar, agent dock collapsed at right. |
| 2 | `clawdaw_agent_open` | Same view, agent dock expanded — Claude conversation in progress, agent has just renamed a track. |
| 3 | `clawdaw_plugin_chain` | One track selected, its plugin chain shown as a horizontal stack of glass cards. |
| 4 | `clawdaw_plugin_open` | A single plugin's parameter view (use a generic compressor — knobs as glass pebbles). |
| 5 | `clawdaw_listening_panel` | The listening layer: instrument classification, key/tempo detection, loudness — calm, informational, not data-vis-overloaded. |
| 6 | `clawdaw_empty_state` | Brand-new empty project. Should be inviting, slightly playful. The hero of "new project" energy. |
| 7 | `clawdaw_transport_detail` | Macro shot of just the transport bar and master meters. This is the "slot machine" moment — most polished tactile detail. |
| 8 | `clawdaw_dark_variant` | Pick the strongest of the above (probably 1 or 2) and produce a dark variant to confirm the palette translates. |

Don't add more shots without asking Sammy. Eight is enough to align on direction.

---

## Prompt template

Use this skeleton for every image. Swap the **Subject** and **Composition** sections per shot.

```
A UI screenshot mockup of CLAWDAW, a modern digital audio workstation.

STYLE: macOS Tahoe (macOS 26) liquid glass design language. Translucent panels
with visible edge refraction and soft inner light. SF Pro typography. Generous
rounded corners (16–24px panels, 8–12px controls). Warm light mode — background
is a slow gradient of peach, dusty rose, and soft amber, with a faint slow
wave pattern reminiscent of the PS2 boot menu. Subtle film grain.

PALETTE: warm neutrals (cream, bone, soft taupe) for surfaces; muted accent
colors (dusty teal, faded coral, pale gold) for active states; deep charcoal
for primary text. Never saturated. Never cold.

MOOD: nostalgic, calm, premium, tactile. The interface should feel like
something you'd want to keep using — a little addictive, like a well-made
slot machine, but never busy.

COMPOSITION: [SUBJECT-SPECIFIC — see per-shot section]

QUALITY: photoreal product render at 2560×1600, sharp text, accurate kerning,
realistic glass refraction, soft ambient occlusion under panels.

NEGATIVE: skeuomorphic wood/metal, brushed aluminum, dark mode (unless
specified), AI-purple gradients, sparkles, cluttered interfaces, fake VU
meters with too many segments, generic stock-photo musicians, hands or
people, Logic Pro chrome, Ableton flat brutalism.
```

### Per-shot composition notes

**1. `clawdaw_main_arrangement`**
Wide arrangement view. 7 horizontal track lanes, each with a name (Drums, Bass, Rhodes, Guitar L, Guitar R, Vocal, Pad). A few audio clips visible as soft glass tiles with simplified waveforms inside (3–5 peaks, not realistic spectrograms). Transport bar pinned top-center: play/stop/record, BPM readout (94 BPM), bar/beat counter (12.3.1), tasteful master meter on the right. A small agent icon in the top-right corner — closed dock state. Playhead is a thin warm-white vertical line at bar 8. No grid clutter — just gentle bar markers.

**2. `clawdaw_agent_open`**
Same scene as #1 but the right-side dock is expanded to ~360px wide. The dock is a tall glass panel with a short conversation: user says "make the rhodes sit better", agent reply visible (one short paragraph, plus a subtle "renamed track 3 to Rhodes (warm)" inline action chip). At the bottom of the dock, a calm input field. The agent's presence should read as a quiet collaborator — no flashy gradient, no robot face. Just type and one small soft-glow indicator dot.

**3. `clawdaw_plugin_chain`**
Mid-shot of a single track row, expanded. Below the row, a horizontal strip of 4 plugin cards: "EQ", "Comp", "Tape", "Reverb". Each card is a small glass pebble ~120×80px with the plugin name and a tiny live readout (e.g., "−3.2 dB GR" on the comp). Add-plugin slot at the end as a dashed-outline glass placeholder. The selected track row above is highlighted with a soft inner glow.

**4. `clawdaw_plugin_open`**
Generic compressor plugin window, floating on top of a blurred arrangement view. Five round knobs in a row: Threshold, Ratio, Attack, Release, Makeup. Knobs as smooth glass pebbles with a single warm indicator notch — NOT skeuomorphic hardware. Below, a small gain-reduction needle that's clearly readable. Single bypass toggle, single preset selector. That's it. No fake spectrum, no waveform display.

**5. `clawdaw_listening_panel`**
A full-width informational strip across the bottom of the arrangement view. Three calm sections: (a) Instrument detection per track ("Rhodes — 94% confidence", "Drums — kick + snare + hats"), (b) Key & tempo ("F minor · 94 BPM · steady"), (c) Mix snapshot ("LUFS-i −16.2 · peak −1.1dBFS · stereo width healthy"). Plain text, generous spacing, no gauges. The agent feels like it's quietly listening. No equalizer-style data-viz noise.

**6. `clawdaw_empty_state`**
Fresh empty project. A single welcome card centered on screen: "New Session" in SF Pro Display, a soft warm illustration above it (abstract — maybe a single floating glass orb with a slow gradient inside, faintly humming). Beneath, two quiet primary actions: "Start recording" and "Import audio". Background is the warm wave-gradient. Should feel hopeful and a little nostalgic — the moment before a song exists.

**7. `clawdaw_transport_detail`**
Tight crop of just the transport bar + master meters, filling most of the frame. This is the showcase shot. Play button as a slightly larger glass pebble, record as a soft coral pebble, BPM and time readouts in SF Mono with clean tabular figures. Two vertical master meters with smooth gradient fills (green→amber→soft-red), capped with peak-hold dots. Should look like the most satisfying single thing in the app — the slot-machine moment. Heavy bokeh blur on the background arrangement behind it.

**8. `clawdaw_dark_variant`**
Take the composition of shot #1 or #2 (whichever read strongest in review). Translate to dark mode: deep warm charcoal background (#1A1614-ish), still with the slow PS2-style wave but inverted to a low-luminance plum/copper gradient. Glass panels become smoked obsidian with the same refraction. Accent colors stay warm but glow slightly more. Confirm legibility.

---

## Workflow for Claude Code

1. **Confirm tools.** Verify the Higgsfield MCP is connected (`mcp__...__generate_image`, `models_explore`, `select_workspace`). If a workspace selector is required, list workspaces and pick the active one. Verify the Figma MCP if we're going to push outputs there.
2. **Pick a model.** Run `models_explore` and choose the best photoreal/UI-capable model available. Note the model name in the output log so we can reproduce. If the workspace has a "design / UI" preset, prefer it.
3. **Generate shots 1–7 in order.** One at a time, not in parallel — read each result before generating the next so you can adjust the prompt for consistency. Especially watch for: (a) palette drifting cool, (b) AI-purple gradients sneaking in, (c) text rendering legibly. If text is garbled in a generation, regenerate or ignore — these are mood pieces, not handoff specs.
4. **Generate shot 8 only after 1–7 are reviewed.** It depends on which composition we like most.
5. **Save outputs** to `docs/design/prototypes/` in the repo with the names listed in the table. Include a one-line caption file (`captions.md`) noting which model + seed produced each image, so we can iterate.
6. **Optional Figma push.** If Sammy says yes, create a new Figma file called `CLAWDAW — Direction v0` and drop the eight images on a single page as an inspiration board. Don't try to recreate them as Figma frames — they're references, not specs.

## Acceptance criteria

A shot is good if:
- It reads as warm, not sterile.
- A non-musician would describe it as "calming" or "satisfying" before "futuristic".
- All visible UI elements correspond to features actually in our roadmap (transport, tracks, plugin chain, agent dock, listening layer). No fictional features.
- It does not look like Logic, Ableton, FL Studio, or Pro Tools.
- The liquid-glass effect is present but never the *subject* — it's the substrate, not the show.

When in doubt: simpler. A shot with three things done beautifully beats a shot with twelve things done passably.

## Notes for the human reviewing

After generation, the review pass is: open all eight, pick the two strongest, identify what made them work, then we either commission a second batch tightening on those two, or move into Figma to start spec'ing the real Phase 3 UI from the winning direction.
