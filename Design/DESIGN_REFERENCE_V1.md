# EvolutionVox — Design Reference V1 (approved, November 17, 2026)

**Source of truth:** the user's selected ivory/cream-and-gold screenshot from the EvolutionVox design conversation. **The screenshot is NOT a ready-to-use layer or UI texture**; do not flatten the knob, bolt, text and control surfaces into one image. This document pins down the behavior and visual composition for implementation.

## Main view
- Landscape cream/ivory chassis, subtle clouds and soft warm-gold atmosphere. Avoid dense decorative detailing.
- Header: bold italic **EVOLUTION** in dark charcoal and **VOX** in champagne gold. Small subtitle **VOCAL TUNING**.
- Main focus: the user's original 3D emoji-like, laurel-crowned Zeus character (NOT photorealistic), centered and holding the lightning.
- Zeus's lightning is **the actual retune pointer**, not merely decoration. Do NOT show a second dial pointer that can disagree with it.
- One 270-degree arc surrounding Zeus. Gold fill follows the bolt tip exactly, with a brass/ivory inactive segment.
- Key selector on left, Scale selector on right; single gold percentage readout beneath Zeus.
- Top-left power/bypass (must truly bypass audio before enabled). Top-right settings/advanced panel, where the non-core controls live.
- Keep default screen simple: Retune, Key, Scale, Power, Settings. Presets and custom-scale keyboard live in advanced settings.

## Canonical control mapping
Existing VST3 automation parameter named retune stays in **0..250 milliseconds**. UI expresses this as intuitively increasing **0..100% intensity**.

| Intensity | Retune ms | Bolt points |
| --- | ---: | --- |
| 0% | 250.00 | lower-left (natural) |
| 25% | 105.47 | upper-left |
| 50% | 31.25 | straight up |
| 75% | 3.91 | upper-right |
| 100% | 0.00 | lower-right (hard tune) |

The precise cubic mapping and angles are defined only in Source/Plugin/RetuneDialMapping.h.
Lightning bolt endpoint, gold arc fill, displayed percentage, pointer hit target and value sent to the DAW **must use the same mapping**. Increasing intensity reduces retune milliseconds. Mouse-drag around arc, mouse-wheel, double-click reset, automation, and project recall must work.

### Implementation notes
- Current LightningRetuneDial.h is a reusable JUCE proof of interaction, **not integrated into PluginEditor yet**. It draws a temporary ZEUS medallion until art is layered; it is not a finished visual.
- For production, supply *transparent, separated* image assets for Zeus WITHOUT a fixed lightning, the separate lightning pointer/hand (or a short pose/spritesheet sequence), static cream-cloud chassis, and a gold/ring texture if desired.
- The hand-to-bolt joint must remain visually attached during rotation. At higher values use the lightning-forward pose, lower values use the lightning-raised pose, with either rigged arm rotation or stepped pose sprites. **Never rotate all of Zeus upside down** to track the dial.
- Full 30–60 FPS animation is unnecessary; update the UI only when parameters change. Avoid image decoding/allocations and rendering on the audio thread. Prefer cached, modest-resolution UI assets.
- The currently uploaded flat reference picture is a *visual target*, not a set of separately interactive components.
- This design work does not claim the retune DSP, latency, or DAW CPU issues have been resolved.
