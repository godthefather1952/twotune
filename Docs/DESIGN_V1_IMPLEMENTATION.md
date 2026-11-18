# EvolutionVox Design V1: implementation notes

Branch `design/v1-polish` (from `design/divine-white-v1-lightning-dial`). `main` (Build 0.1) is untouched.

![Design V1 at 100%: horizontal strike](screenshots/main_100.png)

## Main screen

The main screen has only **Power, Retune (the Zeus dial), Key, Scale and Settings**, plus the logo and the
words NATURAL / HARD TUNE / RETUNE. **The master knob shows no percentage or number**, and no background
artwork contains one.

| Control | Parameter | Notes |
| --- | --- | --- |
| Zeus lightning dial | `retune` (0–250 ms, unchanged) | Intensity 0–100% is internal only (cubic map, `RetuneDialMapping.h`). |
| Key | `key` | Unchanged. |
| Scale | `scale` | Unchanged. Custom notes are edited in Settings. |
| Power | `power` (**new**, default On) | Latency-compensated and click-free. Separate from the host's bypass. |
| Settings | (none) | Opens the advanced panel. |

How the dial responds:

- **Drag on the gold arc:** jumps to that point.
- **Drag on Zeus:** works like a vertical knob (Shift for fine control).
- **Mouse wheel:** adjusts the value.
- **Double-click:** resets to the default (8 ms).
- **Arrow, Page and Home/End keys:** step through the range.
- **Screen readers:** see a slider whose value reads in ms.

## Standalone app

The same processor and editor are also built as a normal macOS application (JUCE Standalone format), so
the interface can be tested without a DAW: `build/EvolutionVox_artefacts/Release/Standalone/EvolutionVox.app`.
The window is resizable. Its Options button picks the audio device. JUCE mutes the audio input by default
to prevent feedback; use headphones before unmuting it. The VST3 is unchanged.

## Settings panel (all Build 0.1 features kept)

The panel holds:

- Retune Speed in **ms**, Tracking, Flex, Humanize and Transpose.
- Input Range and the factory Presets.
- The custom-scale note keyboard (active when Scale = Custom).
- The live input/target note and pitch trace.

A footer states that formant correction and throat modelling are **not** implemented. Esc or DONE closes it.

## How the lightning dial works

`Source/Plugin/ZeusRig.h` (plain C++, unit tested) is the single source of truth for the geometry:

- **Pose:** the nearest of the nine nominal intensities (0, 12, 25, 37, 50, 62, 75, 87, 100), with ±1%
  hysteresis so automation hovering on a boundary doesn't make Zeus flicker.
- **Meter point:** a 270° arc (135° → 405°) from `RetuneDialMapping::pointerDegrees`.
- **Lightning:** a **vector** bolt (`boltGeometry` / `boltOutline`) built from the **pose's fist** to
  **exactly the meter point**: three classic zig-zags, a gently tapered width and a sharp arrow-head tip.
  It's drawn in layers (soft glow, amber edge, gold body, pale-gold and white-hot core). Its proportions
  come from its own length, never from stretching a bitmap, so it stays crisp and undistorted at every
  angle, length and display scale. The bolt *is* the pointer; there is no second knob or pointer.

The meter sweeps exactly 270°, from 6 o'clock (0%, Natural) over the top to **3 o'clock (100%, Hard
Tune)**. The dial centre sits at the same height as pose 9's fist, so at 100% Zeus strikes with the bolt
**horizontal, straight forward to the right**, and its tip meets the end of the gold meter. Pose 9's painted
bolt is already horizontal, so it isn't rotated. The radius (450) is the smallest that keeps the meter clear
of every pose, so the 0% tip at 6 o'clock is never hidden behind Zeus's legs. All of this is tested to within
0.01 px. 50% now points to the upper-left diagonal (pose 5).

The empty lower-right quadrant holds the HARD TUNE label. The area directly **below the dial is reserved
for a future secondary control** (`secondaryControlArea()` in the editor). Nothing is drawn there yet, and
a test checks that it stays empty.

Each pose is drawn in this order: body without the active arm, then the independent lightning, then the
active arm (so the fist covers the grip). The arm is never rotated, so the shoulder seams stay exactly as
painted. Base + arm reproduce `complete_no_bolt` pixel-for-pixel, which the pipeline verifies.

## Asset pipeline (`tools/build_zeus_assets.py`)

- **Source:** `Assets/Zeus/source/ZeusRigV2`, the handoff copied verbatim (manifest and README kept for
  provenance).
- **Output:** `Assets/Zeus/ui/` (27 cropped PNGs, about 1.9 MB, embedded with `juce_add_binary_data`),
  `Assets/Zeus/ui/rig.json`, and the generated `Source/Plugin/ZeusRigData.h`.
- **To regenerate after any art change:** run `python3 tools/build_zeus_assets.py --preview` (needs Pillow and
  NumPy).

| Issue in Zeus Rig V2 | Fix |
| --- | --- |
| The body jumps sideways between poses (up to ±50 px) | Each pose is shifted by whole pixels so the body's centre of mass stays put (the feet baseline was already constant). |
| A stray gold fragment above pose 8's head | Islands detached from the whole figure (base ∪ arm) are removed. |
| The source bolts are clipped (pose 5, tip y = −20), have blunt dark-edged grips, and point in directions that don't fit a 270° meter | The plug-in draws its own vector lightning instead. The source bolt PNGs are kept only for provenance. |
| White matte halo around each arm/fist (shows against the gold bolt) | Near-white pixels on the figure's outer edge of the arm layer are removed (never on the arm/body seam). |
| Shoulder gaps | The arm layers are never rotated, so registration is preserved. |

## Parameters, automation and recall (all covered by `EvolutionVoxPluginTests`)

- **Existing parameters:** all nine Build 0.1 parameter IDs, versions, order, ranges, defaults and
  normalisation are unchanged, so existing automation lanes and projects map to the same values.
- **`retune` snapping:** the 0.1 ms snapping interval was removed, so the value is now continuous. Normalised
  and stored values are unchanged. Before, the top ~7% of the dial could only reach 0.2, 0.1 or 0 ms, so the
  bolt jumped.
- **New `power` parameter:** added **last**. Migration: projects without it load with Power = On, even into
  an instance that was switched off.
- **Host bypass:** still the VST3 wrapper's own bypass, with the same VST3 parameter ID as Build 0.1. It now
  outputs dry audio delayed by the plug-in's latency instead of undelayed dry audio.

## Performance

**Audio thread:**

- No allocation, locks, image work or UI calls.
- With Power off, the tuner doesn't run at all; the output is the input delayed by the 2048-sample latency.
- Turning Power on resets the tuner, warms it up for the latency period while the dry signal plays, then
  crossfades over 20 ms.

**Adopted from `Performance_UI_0.2.patch` after measuring:**

- Coefficient caching.
- `exp2` every 16 samples, with interpolation.
- A Hann lookup table.
- No per-sample atomic loads.
- A sustain counter that uses the real hop size.

The output and test results are unchanged.

**Not adopted: the patch's detector change** (96 Hz analysis and stride-2 YIN). It is the patch's biggest
CPU saving (about 2.4× on the DSP benchmark), but it halves how often pitch is analysed (5.3 → 10.4 ms between
estimates), which can add audible lag in hard-tune. See *Decisions needed* in the PR.

**UI thread:**

- PNGs decode lazily through JUCE's shared `ImageCache`, once per process across all instances.
- Sprites are pre-scaled once per display scale and drawn as 1:1 blits.
- The meter track and the chassis are cached.
- **One update path for every input.** Click, drag, wheel, keys, accessibility and DAW automation only
  set the parameter. A frame timer reads it and updates the picture: **60 fps maximum while something is
  moving**, then after 0.4 s idle it drops to a 15 Hz poll of two atomics that draws nothing.
- **Exact dirty regions.** Each frame repaints the old and new bolt plus the changed meter segment.
  A pose or power change repaints the dial. Every layer depends only on the current value (the meter's
  gradient is fixed across the dial), so a partial repaint is **pixel-identical** to a full one. This is
  verified for click, drag, wheel and automation at 17 values by `EvolutionVoxPluginTests`.
- The live pitch display updates only while Settings is open. On the main screen nothing animates, so an idle
  open editor costs essentially nothing.

### Measurements (MacBook Air, Intel i5-5350U 1.8 GHz, macOS 12.7, Release build)

These are offline measurements on one machine, **not** DAW measurements. They are repeatable with
`EvolutionVoxPluginTests --bench`.

`processBlock`, stereo, as a percentage of real time on one core:

| Sample rate | 64 | 128 | 256 | 512 | 1024 |
| --- | --- | --- | --- | --- | --- |
| 44.1 kHz, Power on | 1.28 | 1.31 | 1.23 | 1.23 | 1.27 |
| 48 kHz, Power on | 1.41 | 1.35 | 1.35 | 1.33 | 1.33 |
| 96 kHz, Power on | 1.76 | 1.66 | 1.63 | 1.63 | 1.60 |
| any rate, Power off | ≤ 0.07 | | | | |

DSP-only benchmark (`evolutionvox_perf_bench`, 8 s, 48 kHz stereo): Build 0.1 took 0.120 s; this branch takes
about 0.115 s. With the patch's detector change it would take 0.051 s (not adopted).

Editor, software renderer, 1000×650: a full-window repaint takes about 6 ms, and a full dial repaint about 4 ms.

- **Drag sweep:** each frame repaints on average 13% of the dial in about 1.1 ms. At 60 fps that's at most
  about 6.6% of one core (an upper bound: macOS's CoreGraphics renderer is usually faster).
- **Idle:** a poll costs about 0.002 µs and never repaints.
- **Standalone app idle:** measured at about 3.4% of one core, almost all of it the live audio device and
  DSP.

The DSP was never likely to be the cause of the original DAW slowdown (about 1.3% of one core). The Build 0.1
editor, however, called `setColour` on all 12 note buttons 20 times a second, and each call repaints a button.
Those repaints merged with the pitch-plot repaint into a large region redrawn 20 times a second while the
window was open. That is gone. Confirming the DAW behaviour needs a measurement in your DAW (see the test plan
in the PR).

## Repaint glitch fix (dragging / scrolling)

**Symptom:** a direct click on the arc looked right, but dragging, scrolling or automation left the bolt
jagged and fragmented, with white and stale fragments.

**Causes,** reproduced pixel-exactly by `EvolutionVoxPluginTests --repaint-experiment`, which emulates the
OS's partial repaints and compares them with a full render:

1. **Dirty-region bug:** `dirtyAreaFor()` grew the bolt's bounding box from a single point. JUCE treats a
   zero-size rectangle as *empty*, so each corner replaced the previous one, and the "bolt area" was an
   8×8 box. The old bolt was never erased. A click's big meter segment happened to cover it, which is why
   clicks looked fine.
2. **Position-dependent fill:** the meter gradient ran from the arc start to the *current* tip, so moving
   the tip recoloured the whole lit arc, but only the dirty part was repainted.
3. **Jagged bolt:** the bitmap bolt was resampled every frame while stretched up to 2.3× in one direction.

**Before the fix:** forced full repaints showed 0 stale pixels on every route, while dirty-region repaints
showed up to 20,503 stale pixels (colour difference up to 249/255) for drag, wheel and automation.

**After:** 0 stale pixels on every route at all 17 values, and click and scroll renders of the same value
are identical (largest difference 1/255).

## Known limitations

- **Source art:** pose 8's toga and pose 9's torso, toga and legs have rectangular, stair-stepped holes in the
  alpha mask (a block-mask background removal). The holes contain no colour, so they can't be repaired
  without new art. There is also a white matte fringe around every pose; it's invisible on ivory but would
  show on a dark background.
- **The poses are regenerated art,** not pixel-identical to the previously approved illustration (per the
  handoff). They need art-direction sign-off.
- **The lightning is drawn by the plug-in, not taken from the supplied art.** It matches the art's colours and style but isn't a hand-painted bolt. Swapping in pose-specific painted bolts later would need art drawn for each angle.
- **Hard tune saturates early:** above about 87% intensity (below 0.5 ms) the DSP already behaves as
  instant hard tune, so the last part of the arc sounds identical (the existing DSP behaviour).
- **No DAW testing yet:** the VST3 builds and the headless tests pass, but it hasn't been checked inside a
  DAW, and DAW CPU hasn't been measured.
- **No AU format:** VST3 and Standalone only.
- **The standalone app is unsigned:** it opens on the Mac that built it. On another Mac, Gatekeeper needs right-click → Open.
- **Zeus is drawn smaller than before:** the meter must clear his feet at 6 o'clock and reach his fist height at 3 o'clock, which makes the dial larger relative to him.
