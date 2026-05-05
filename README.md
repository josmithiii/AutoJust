# AutoJust

Master-bus adaptive Just Intonation retuner — an auto-adJusting plugin that nudges resolved spectral peaks onto a slowly-drifting JI grid. Conceptually: *Auto-Tune for an entire polyphonic mix*, with no key knowledge required.

The hard problem in adaptive JI — cross-instrument disagreement — disappears at the master bus, since one global grid covers the whole mix. Unresolved content (transients, breath, reverb tails, vibrato excursions) passes through untouched.

## License

GPLv3 — see [`LICENSE`](LICENSE). Built on JUCE (GPLv3 / commercial).

## Status

| Step | What | State |
|---|---|---|
| v0.1 | Plugin scaffold — Standalone + AU + VST3, PGM GUI, stub params | done |
| v0.2 | Streaming STFT identity round-trip + unit test | done |
| v0.3 | Peak picker + IF estimation + diagnostics | done |
| v0.4 | Tonic estimator (histogram + LPF + drift) | done |
| v1a  | Single-peak retune (Laroche-Dolson differential phase rotation) | done |
| v1b  | All-peaks retune onto 5-limit JI grid relative to moving tonic | done |
| v2   | Harmonic grouping + multiple grid types | next |
| v3   | Scala loader, MIDI key hint, scope/visualization | |

As of v1b, AutoJust does the headline thing: every resolved spectral peak is softly pulled toward the nearest 5-limit Just Intonation ratio relative to the moving tonal center. With `bypass=true` (default GUI state) the plugin is identity-passthrough via STFT round-trip; flip `bypass=false` and dial in `snapStrength` to engage retuning. Audio comes out delayed by 4096 samples (~93 ms at 44.1 kHz, fully compensated by every modern DAW's PDC — see latency section below).

## Build (standalone repo)

```sh
git clone --recurse-submodules https://github.com/josmithiii/AutoJust.git
cd AutoJust
cmake --preset release
cmake --build build/release --parallel
```

Build outputs:
```
build/release/AutoJust_artefacts/Release/Standalone/AutoJust.app
build/release/AutoJust_artefacts/Release/AU/AutoJust.component
build/release/AutoJust_artefacts/Release/VST3/AutoJust.vst3
```

Universal binary on macOS (x86_64 + arm64).

## Build (inside jos-juce-plugins research workspace)

```sh
make ajr     # release standalone
make ajd     # debug standalone
make ajro    # run release standalone
make ajau    # build AU plugin (release)
make ajaui   # build + install AU to ~/Library/Audio/Plug-Ins/Components
```

## Tests

Build any test target then run the produced binary. Standalone repo path shown:

```sh
cmake --build build/release --target AutoJust_StftTest --parallel
./build/release/tests/AutoJust_StftTest_artefacts/Release/AutoJust_StftTest
```

Available test targets:

| Target | What it checks |
|---|---|
| `AutoJust_StftTest` | STFT round-trip reconstruction error ≤ 6e-7 (sine/sweep/noise/impulse/DC) |
| `AutoJust_PeakAnalyzerTest` | Peak picking + IF estimation (pure tones to ≤ 0.001 cents) |
| `AutoJust_TonicEstimatorTest` | Tonic histogram settles, drift cap honored |
| `AutoJust_RetunerTest` | Manual single-peak L-D shift (output at 0.000c) |
| `AutoJust_TuningGridTest` | 5-limit JI grid math + circular shortest path |
| `AutoJust_RetunerJITest` | End-to-end ET dyad → JI dyad (E pulled to 5/4 at 0.004c) |

Inside `jos-juce-plugins`, the artefact path is prefixed with `Effects/AutoJust/`.

## Layout

```
CMakeLists.txt          JUCE plugin target (Standalone + AU + VST3)
Source/
  PluginProcessor.{h,cpp}   foleys::MagicProcessor — params + analyzer wiring
  Stft.{h,cpp}              streaming Hann² OLA, default identity
  PeakAnalyzer.{h,cpp}      Stft subclass: peak pick + IF, exposes snapshot
  TonicEstimator.{h,cpp}    cents-mod-octave histogram + drift-limited slew
  TuningGrid.{h,cpp}        abstract grid + JustGrid5Limit (5-limit JI)
  Retuner.{h,cpp}           PeakAnalyzer subclass: L-D phase rotation +
                            JI soft attractor on all peaks
Resources/
  Layouts/AutoJust.xml      PGM GUI XML
tests/                      Catch2-free standalone console executables
```

## Latency and DAW compatibility

AutoJust reports 4096 samples of latency (≈ 93 ms at 44.1 kHz) via `setLatencySamples()`. This is normal for a master-bus / mixing plugin and gets fully compensated by every modern DAW's Plugin Delay Compensation (PDC).

**Mixing / playback / bounce** — fully aligned, no audible offset. Logic Pro's PDC has a multi-second ceiling, so 4096 samples is trivial. Same for Pro Tools, Cubase, Studio One, Reaper, Ableton Live.

For reference, comparable mastering plugins:

| Plugin | Latency |
|---|---|
| FabFilter Pro-Q 3 (linear phase) | 60–100 ms |
| iZotope Ozone modules (linear phase) | 100+ ms each |
| Soothe2 / Gullfoss (some modes) | 20–100 ms |
| Look-ahead limiters | 1–50 ms |
| **AutoJust** | **93 ms** |

**Where 93 ms is too much:**

- **Live tracking through AutoJust** (vocalist hearing themselves through it): >20–30 ms feels disconnected. Logic's *Low Latency Mode* during record automatically bypasses plugins over its threshold, so AutoJust would drop out — which is what you want, since adaptive retuning during tracking is the wrong workflow anyway.
- **Live standalone performance**: same issue. AutoJust is a mix-bus tool, not a live-performance one.

If a low-latency variant becomes useful later, the knobs are: smaller FFT (2048 → ≈ 46 ms, but half the frequency resolution at low pitches), or a smarter front end (reassigned spectrogram, analytic-signal IF tracking). Not worth optimizing until v1 reveals whether the longer window is actually needed for stable peak detection.
