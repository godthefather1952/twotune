# EvolutionVox — Development Build 0.1

Independent C++/JUCE project for a **real-time monophonic vocal pitch corrector**. This project does not contain any Antares code, names, binaries, assets, or copy-protected resources.

## Design V1 interface (branch `design/v1-polish`)

Ivory and champagne-gold interface. Zeus's lightning bolt is the Retune pointer, and the main screen has only
Power, Retune, Key, Scale and Settings. The VST3 and Standalone app build, and the tests pass on macOS and Windows CI.
**It has not yet been checked in a DAW.** See [Docs/DESIGN_V1_IMPLEMENTATION.md](Docs/DESIGN_V1_IMPLEMENTATION.md)
for how it works, measurements and known limitations. Zeus artwork: `Assets/Zeus/source` (original) is turned
into `Assets/Zeus/ui` (optimized) by `tools/build_zeus_assets.py`.

**Standalone app (macOS):** the build also produces
`build/EvolutionVox_artefacts/Release/Standalone/EvolutionVox.app`. It's the same interface without a DAW:
double-click it, or run `open build/EvolutionVox_artefacts/Release/Standalone/EvolutionVox.app`. Choose your
audio device under **Options**. Audio input starts muted to avoid feedback, so use headphones before unmuting.

![Design V1](Docs/screenshots/main_050.png)

## Current status

**DSP test suite: passing on Linux using synthetic sine-wave test cases.**

**VST3 plugin source: written, but not compiled or tested in a DAW in this environment.** The required JUCE SDK and an appropriate macOS/Windows build host were unavailable. Treat this as early prototype code, not a usable commercial release.

### Implemented in code

- Monophonic pitch detection: decimated YIN/cumulative mean normalized difference
- Real-time tonal target selection with key and scale
- Chromatic, Major, Natural Minor, Harmonic Minor, Minor Pentatonic and Custom modes
- Interactive 12-note mask/piano-style pitch-class selector
- Retune Speed (0–250 ms)
- Input-range selection and tracking sensitivity
- Hard tune at zero Retune Speed
- Experimental Flex and Humanize behaviors
- ±12 semitone transpose (at DSP layer; shifting quality still needs vocal tests)
- Experimental dual-grain pitch shifter (not formant preserving)
- Host parameter save/load, four factory presets, live detected-pitch trace
- JUCE-based VST3 processor, UI, host automation for parameters

### Not implemented yet (selected V1 scope)

- True formant-correction engine
- Vocal tract/throat-length controls
- Explicit natural-vibrato analysis and preservation
- Measured/optimized low latency across voice ranges
- Validated audio quality across real voices, breath, rasp, consonants, and vibrato
- Compiled and verified VST3 installation

### Intentionally excluded from V1 scope

MIDI correction, generated vibrato, a manual/graphical pitch editor, individual note editing, note segmentation, time stretching, or timing correction.

## Build the VST3 locally

You need CMake 3.22+, Ninja or a native CMake generator, C++17, and a macOS or Windows development machine.

JUCE is **not bundled**. CMake is configured to fetch JUCE `8.0.17-security` from GitHub, requiring internet access and compliance with the JUCE license. Alternately pass `-DJUCE_DIR=/absolute/path/to/JUCE` to use a local copy.

### macOS (Apple Silicon or Intel)

```bash
xcode-select --install
brew install cmake ninja
git clone https://github.com/juce-framework/JUCE.git -b 8.0.17-security --depth 1 JUCE
cmake -S . -B build -G Ninja -DJUCE_DIR="$PWD/JUCE" -DEVOLUTIONVOX_BUILD_PLUGIN=ON
cmake --build build --config Release
```

The resulting `.vst3` bundle will typically be under `build/EvolutionVox_artefacts/VST3/`. To test in FL Studio or Ableton, copy it to `~/Library/Audio/Plug-Ins/VST3/`, rescan plugins, and test on a duplicate vocal track.

### Windows

Install Visual Studio 2022 (C++ desktop workload), CMake and Git; then open a Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The `.vst3` is usually under `build/EvolutionVox_artefacts/Release/VST3/` (path varies by generator). Copy the bundle to `%CommonProgramFiles%\VST3` and rescan in your DAW.

## Test DSP only (no JUCE SDK needed)

```bash
cmake -S . -B build-dsp -DEVOLUTIONVOX_BUILD_PLUGIN=OFF -DEVOLUTIONVOX_BUILD_TESTS=ON
cmake --build build-dsp
ctest --test-dir build-dsp --output-on-failure
```

## Important DSP limitations

- This is a first-pass granular, pitch-synchronous-ish delayline algorithm. It passes **pure-tone** accuracy tests, not a comprehensive vocal-quality evaluation.
- Monophonic pitch detection may fail on drums, polyphonic input, noisy breaths, or strong harmonics.
- Pitch shifter uses a nominal `2048`-sample host latency (42.7 ms at 48 kHz), **not low enough for all live-monitoring situations**. Tracking additionally requires a history window before its first result.
- Formants are **not** preserved while pitch shifting. Do not mistake the current feature set for the approved V1 spec.
- Stereo is analyzed from channel 1 and both channels use the same correction ratio; this is tuned for centered mono vocal tracks.
- Low frequencies and very large shifts may produce artifacts. No real-time lock-free fuzz testing has been done.
- Need test and refine note switching, voiced/unvoiced boundaries, pitch detector confidence, CPU usage, preset recall, and automation before release.

## Licensing

The original code in this project is provided for development and evaluation. JUCE is an external dependency distributed under its own license; choose a compatible JUCE commercial or open-source license before distribution. No proprietary Auto-Tune components are included.

## Project organization

```text
Source/DSP/PitchDetector.h    YIN-based monophonic detector
Source/DSP/PitchShifter.h     Experimental dual-head granular shifter
Source/DSP/TunerEngine.h      Note/scale targeting and correction smoothing
Source/Plugin/PluginProcessor.*  JUCE VST3 processor and state
Source/Plugin/PluginEditor.*     JUCE visual controls and pitch trace
Tests/test_dsp.cpp            Offline DSP automated tests
CMakeLists.txt                Cross-platform configuration
```
