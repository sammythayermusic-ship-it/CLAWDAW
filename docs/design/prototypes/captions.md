# CLAWDAW — Direction v0 Prototypes

Generated 2026-05-09 per [`docs/design-prototype-prompt.md`](../../design-prototype-prompt.md).

## Setup

- **Model:** `nano_banana_2` (Google Nano Banana Pro) — picked for photoreal + text rendering + 4K + diagrams. The other text-only options on Higgsfield (Z Image, Soul Cast, Soul Location) are stylized / character / environment models, not UI-grade.
- **Workspace:** private (only one available, `267b68e0-4f1e-4eb6-b0f0-f53771ddf67f`, plan: ultimate, 1231.5 credits at start).
- **Standing params:** `aspect_ratio: 16:9`, `resolution: 4k` (renders out at 5504×3072), `count: 1` per shot. No reference images — text-to-image only.
- **Workflow:** generated sequentially, one at a time, reviewing each before kicking off the next. Prompt for each subsequent shot referenced "the same warm peach/rose palette as the prior shots" to hold continuity.
- **Total generations:** 8 (one per shot, no regenerations needed).
- **Seeds:** Higgsfield does not return a seed in the job payload. If we need to reproduce a specific image, the canonical pointer is the job ID listed below.

## Shots

| # | File | Job ID | Notes |
|---|------|--------|-------|
| 1 | [`clawdaw_main_arrangement.png`](clawdaw_main_arrangement.png) | `15994de2-0167-446b-80d4-cf0296d08d75` | All 7 track names rendered correctly. Transport reads "94 BPM" + "12.3.1". Agent dot in top-right. Waveforms abstract (3-5 peaks). Aesthetic locked here — every later shot referenced this palette. |
| 2 | [`clawdaw_agent_open.png`](clawdaw_agent_open.png) | `8897bf4f-8e5d-48d0-9a2b-be5edbc28757` | Agent dock with "make the rhodes sit better" + agent reply paragraph + "Renamed track 3 → Rhodes (warm)" action chip + "Ask the engineer…" input. **Deviation:** waveforms came back more detailed/realistic than shot 1's abstract bumps — slight inconsistency with shot 1's clip art. **Deviation:** the soft inner-glow highlight ended up on the Bass track instead of Rhodes. The action chip still reads correctly so the story holds. |
| 3 | [`clawdaw_plugin_chain.png`](clawdaw_plugin_chain.png) | `f01d3e19-ded8-4099-9fd1-59757a187ed0` | Rhodes selected with subtle inner glow. Four plugin pebbles (EQ, Comp -3.2 dB GR, Tape sat 35%, Reverb plate · 1.4s) + dashed "Add plugin" placeholder. **Deviation:** the model included literal "SF Pro" / "SF Mono" labels inside cards as visible text, treating typography notes as content. Acceptable for mood. |
| 4 | [`clawdaw_plugin_open.png`](clawdaw_plugin_open.png) | `76fcbc8e-2ca1-4dfa-8b6e-92724434370a` | Floating Compressor window: 5 ceramic pebble knobs (Threshold -18.4 dB / Ratio 4.0:1 / Attack 8 ms / Release 120 ms / Makeup +3.2 dB), bypass pill toggle, "Default ▾" preset, gain reduction needle "-3.2 dB GR". Background blur preserves the prior arrangement view (CLAWDAW wordmark visible in the bokeh). **Note:** the knob indicator notches are subtle dimples rather than the requested pale-gold color, but the calm/non-skeuomorphic intent is preserved. |
| 5 | [`clawdaw_listening_panel.png`](clawdaw_listening_panel.png) | `1153c6f8-4c9e-4dac-9609-588f345b2b6f` | Three sections (Instrument Detection / Key & Tempo / Mix Snapshot). All four detection lines render correctly. "F minor · 94 BPM · steady" reads as written. LUFS-i / Peak / Width column in SF Mono. No data-viz noise — exactly the calm informational read I wanted. |
| 6 | [`clawdaw_empty_state.png`](clawdaw_empty_state.png) | `df8c27e9-11b3-4eb5-a43a-6236e33318b2` | Floating glass orb with dusty teal/peach gradient and warm halo above the "New Session" card. Subtitle "Start with a blank page. Or just hit record." renders. Two pill buttons: "Start recording" (coral) / "Import audio" (cream). CLAWDAW wordmark top-left, agent icon top-right. Genuinely hopeful read. |
| 7 | [`clawdaw_transport_detail.png`](clawdaw_transport_detail.png) | `06af35a0-b472-4336-b6af-77b58d0a6599` | The slot-machine showcase. Single pill bar floating in heavy peach bokeh: play (slightly larger, gold-glow) / stop (square) / record (coral pebble) / divider / "94 BPM" / divider / "12.3.1 BAR.BEAT.SUB" / divider / two vertical meters with smooth teal→amber→rose gradients + pale-gold peak-hold dots / "-2.4 dB" master readout. Strongest single composition of the set. |
| 8 | [`clawdaw_dark_variant.png`](clawdaw_dark_variant.png) | `9dee588c-4f2c-458d-be0d-74a616271c6e` | Dark variant of shot 1 (Sammy's pick: I went with shot 1 over shot 2 because shot 1 covers the most surface area for testing palette translation — full arrangement, transport, meter, agent icon, all in one frame). Warm charcoal base with copper/plum undertones — successfully avoids cold blue-black. Smoked obsidian glass, ivory text, copper-glow waveforms, coral record button. Reads as "warm room at dusk." |

## Dark set (added after Sammy's "I like the dark as well, keep making more")

After the initial round, Sammy approved the dark direction and asked for dark variants of the rest. Same model, same params, same workflow — sequential, one at a time, each prompt referencing the prior dark shots for continuity. The full dark set now mirrors the light set.

Note: shot 1's dark variant kept its original filename (`clawdaw_dark_variant.png`). All other dark shots use the `_dark.png` suffix.

| Light source | Dark file | Job ID | Notes |
|---|---|---|---|
| Shot 2 | [`clawdaw_agent_open_dark.png`](clawdaw_agent_open_dark.png) | `b7c5921d-6e1a-4ca6-acb3-8c3f1260ec23` | Rhodes track properly highlighted this time (the light version drifted to Bass). Coral user bubble, smoked-glass agent reply paragraph, action chip with copper-glow checkmark. The agent dock dark-mode test — passes, reads as a quiet collaborator, no AI-flashy glow halos. |
| Shot 3 | [`clawdaw_plugin_chain_dark.png`](clawdaw_plugin_chain_dark.png) | `a162ce4d-f465-4ee6-bb2e-e60734919b92` | Rhodes selected with warm-amber inner glow, smoked-obsidian pebbles for EQ/Comp/Tape/Reverb with gold indicator dots, dashed Add-plugin placeholder. The "no literal SF Pro/SF Mono labels" negative prompt landed — clean. |
| Shot 4 | [`clawdaw_plugin_open_dark.png`](clawdaw_plugin_open_dark.png) | `7f4bdbbf-2b1f-4106-b115-053627d732c5` | Knobs catch warm light from above with thin gold notches, coral gain-reduction needle, blurred plum/copper background. Reads as "ceramic instrument panel sitting in a dimly-lit warm room" — exactly the brief. |
| Shot 5 | [`clawdaw_listening_panel_dark.png`](clawdaw_listening_panel_dark.png) | `a6690486-fbf5-4fda-aeb8-ee006b87fb1c` (regen) | Regenerated after Sammy's "this is the vibe" feedback to fix the duplicate-arrangement issue from the first pass. Single unified arrangement now, track headers down the left only once, listening panel sections all clean. Original gen was `ebbc2b59-a476-452f-bc23-5d3381d91635` — overwritten in place. |
| Shot 6 | [`clawdaw_empty_state_dark.png`](clawdaw_empty_state_dark.png) | `f02461d1-5f1a-4b24-ad03-34339acc0704` | The orb glows like a small captured sun against the dark — beautiful. Dim CLAWDAW wordmark, coral "Start recording" pill, dark-glass "Import audio" pill. PS2-style wave pattern visible behind. May be the strongest shot in the dark set on pure mood. |
| Shot 7 | [`clawdaw_transport_detail_dark.png`](clawdaw_transport_detail_dark.png) | `ab57a3dd-eb8a-48b2-b3cc-ef32aa95b2ae` | The slot-machine moment in dark mode. Smoked-obsidian pill bar floating in heavy warm-dark bokeh: gold-glow play / square stop / glowing-coral record / "94 BPM" / "12.3.1 BAR.BEAT.SUB" / two vertical meters with gradient teal→amber→rose fills + gold peak-hold dots / "-2.4 dB". Looks like a small ceramic dashboard glowing softly in a warm dim room. The strongest tactile composition in the dark set. |

**Total dark generations:** 9 (1 from the original batch + 6 in Sammy's "keep making more" pass + 1 listening-panel regen + 1 ultrawide hero, both after Sammy said "this is the vibe I'm feeling").

### Bonus shot — added after vibe confirmation

| File | Aspect | Job ID | Notes |
|---|---|---|---|
| [`clawdaw_hero_dark_ultrawide.png`](clawdaw_hero_dark_ultrawide.png) | 21:9 | `6f49c87f-aada-4771-ae7a-943277bdf7a9` | Marketing-grade hero composition. **The model interpreted "ultra-wide" as "show this on a curved ultrawide monitor in a warm-lit room"** rather than just rendering the UI at 21:9 — the result is a photographic product shot with ambient room lighting around the screen. Unexpected but actually great: this is exactly the kind of image that lives on a landing page. Text inside the screen is small (it's a hero, not a spec); the *mood* is the win. |

**Dark-set winners** (if commissioning a tighter v1 in dark mode):
1. **`clawdaw_transport_detail_dark.png`** — same reasoning as the light shot 7. This is the composition that defines the *feel* in dark mode.
2. **`clawdaw_empty_state_dark.png`** — the orb-against-dark glow is the most distinctive visual moment in either set. Strong candidate for marketing/landing-page hero.
3. **`clawdaw_dark_variant.png`** (shot 1 in dark) — best establishing layout in dark mode.

## What consistently failed

Nothing failed outright. The seven warm-light shots all came back close enough to spec on the first try that no regeneration was needed. The few imperfections are minor and listed under each shot above. Two patterns worth noting:

1. **Literal interpretation of typography hints.** Shot 3 turned "SF Pro / SF Mono" notes into visible label text inside the plugin cards. If we tighten on this aesthetic, drop the typography family from the per-shot prompt body and keep it in the style block only.
2. **Highlight target drift.** Shot 2's "track 3 highlighted" instruction landed on the Bass track instead. The model is good at *one* highlighted thing per shot but not great at picking which one. For tighter handoff specs, point at the highlight by visual position rather than track name.

## Recommended direction for a tighter second batch

If we want to commission a tighter v1 to lock the visual language, base it on:

- **Shot 7 (`clawdaw_transport_detail`)** — the strongest single composition. The pill-shaped transport bar in heavy peach bokeh nails the "satisfying tactile detail" target better than any other shot. This is the shot that defines the *feel* of the design language.
- **Shot 1 (`clawdaw_main_arrangement`)** — the cleanest full-app composition. Shot 2 covers similar ground but the agent dock added visual weight on the right that pulls attention away from the layout itself. For a layout/system shot, shot 1 is the better foundation.

These two together cover the "macro showcase" and "establishing layout" needs. A v1 batch could iterate on these two with: a) tightened track-clip waveform style (pick shot 1's abstract bumps OR shot 2's realistic peaks and stick to one), b) explicit pale-gold knob/notch accents to match the spec's accent-color system, and c) the mute/solo and pan controls scaled up enough to read as tactile.
