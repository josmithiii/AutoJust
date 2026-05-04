# AutoJust — Plan

> **AutoJust**: an auto-adJusting master-bus retuner that nudges resolved spectral peaks onto a slowly-drifting Just Intonation grid. By the time we're done supporting every tuning system worth supporting, the slogan writes itself: *just what you want*.

## Goal

A JUCE master-bus / aux-bus effect that listens to a polyphonic mix and gently retunes well-resolved harmonic content toward a chosen tuning grid (default: 5-limit Just Intonation), with the grid's tonal center drifting slowly to follow the music. Unresolved content (transients, noise, breath, reverb tails, vibrato excursions) passes through untouched.

The novel part — relative to Auto-Tune (monophonic), Melodyne (offline + manual), and Hermode (per-synth, can't unify a multi-instrument mix) — is **polyphonic, real-time, mix-wide, automatic** retuning. One global grid for the whole mix means cross-instrument coherence comes for free.

## Non-Goals (v1)

- Not a melodic pitch corrector. We don't move notes onto a scale; we shrink intervals between concurrent partials toward consonant ratios.
- Not zero-latency. Mixing-grade plugin (~100–200 ms latency acceptable).
- Not a creative effect. The success criterion is "you only notice it when you bypass it."
- No MIDI input in v1. Fully audio-driven. (MIDI-side-chain key/chord hints are a v3 idea.)

## Signal Flow

```
in ──► STFT ──► peak detect ──► quality gate ──► harmonic grouper
                                                       │
                                                       ▼
                                               grid + tonic estimator
                                                       │
                                                       ▼
                                              per-group cents shift
                                                       │
                                                       ▼
                                       peak-locked spectral translation
                                                       │
                                                       ▼
                                                    ISTFT ──► out
```

### 1. STFT front end
- Hann window, 4096 samples @ 44.1k (≈ 93 ms), 75% overlap (hop 1024).
- Sample-rate aware: scale window to keep ≈ 90 ms at any SR.
- Use phase-derivative method for instantaneous frequency per bin (or full reassignment if cheap enough).

### 2. Peak detection
- Local magnitude maxima above noise-floor estimate (per-octave median + margin).
- For each peak, record: bin, instantaneous frequency, magnitude, and main-lobe shape error.

### 3. Quality gate ("well-resolved peak" criterion)
A peak is eligible for retuning iff **all** of:
- (a) Magnitude ≥ noise-floor + threshold (e.g. +12 dB).
- (b) IF stable across last N frames: cents-variance below threshold. This is the key gate — it rejects vibrato-instant, transient, and reverb-tail content.
- (c) Main-lobe shape matches analysis window's known mainlobe within tolerance (rejects bins corrupted by adjacent-peak interference).

Peaks that fail any gate are passed through untouched.

### 4. Harmonic grouping
- Greedy: starting from highest-magnitude unassigned peak, scan downward in frequency for sub-harmonic candidates, then build the harmonic stack at integer multiples (with tolerance for inharmonicity).
- Group → one f₀ estimate, one cents-correction, applied identically to all members.
- Why: real strings have inharmonicity B ≈ 10⁻⁴; snapping each partial independently to a strict-octave grid would *correct* the inharmonicity and produce a synthetic "DX-7-ish" timbre. Group-shift preserves source timbre.

### 5. Tonic + grid estimator
- Long-time histogram of well-resolved peak frequencies modulo 1200 cents (one octave).
- Weighted by peak magnitude × dwell time.
- LPF the histogram peak position with τ ≈ 2–5 s → the moving tonic.
- Rate-limit drift (e.g. ≤ 1 cent per 100 ms) to keep retuning inaudible as motion.
- Grid: 5-limit lattice positions {1/1, 16/15, 9/8, 6/5, 5/4, 4/3, 45/32, 3/2, 8/5, 5/3, 9/5, 15/8} (octave-equivalent) relative to tonic. Configurable.

### 6. Per-group cents shift (soft attractor, not snap)
For each group's f₀ in cents:
- `target = nearest grid point in cents`
- `delta = target − f₀`
- `shift = sign(delta) · min(|delta|, max_pull) · gate(|delta|)`
  - `max_pull`: e.g. 8 cents/frame ceiling so corrections smooth in.
  - `gate(d)`: zero outside ±50 cents (don't drag a wildly off-grid peak across), ramp up linearly inside.
- Hysteresis: once attracted to a grid point, widen the basin slightly so peaks near a grid boundary don't flicker between neighbors.

### 7. Peak-locked spectral translation (Laroche–Dolson)
- For each group: shift each member peak's bin region by `cents_shift` (translated to bin-fraction).
- Region = bins within ± mainlobe-width of peak center.
- Translate the whole region as a unit, preserving intra-region phase relationships; rotate phases by `2π · hop · Δf / SR` per frame to maintain horizontal phase coherence.
- Bins not in any peak region: pass through unchanged.

### 8. ISTFT
- Standard overlap-add with synthesis window.

## Architecture / Code Layout

```
Effects/AutoJust/
  AutoJust_PLAN.md            ← this file
  CMakeLists.txt
  Source/
    PluginProcessor.{h,cpp}   ← JUCE AudioProcessor
    PluginEditor.{h,cpp}      ← Foleys PGM (XML layout) like the instruments
    Stft.{h,cpp}              ← STFT/ISTFT scaffold (analysis + synthesis windows, OLA)
    PeakPicker.{h,cpp}        ← peak detect + IF + quality gate
    HarmonicGrouper.{h,cpp}   ← grouping into harmonic stacks
    TuningGrid.{h,cpp}        ← grid + tonic estimator + drift; abstract base
                                with Subclasses: JustGrid5Limit, EqualTemperament,
                                Pythagorean, Meantone, …
    PeakShifter.{h,cpp}       ← Laroche–Dolson region translation
  Resources/
    Layouts/AutoJustEdit.xml  ← PGM GUI layout
  tests/                       ← Catch2 unit tests (or use jos-modules tests/)
```

Reuse what we already have where possible: STFT primitives may already exist in `submodules/jos-modules/jos_dsp/`; check before duplicating.

## Tuning System Plug-in Architecture

`TuningGrid` is an abstract interface:
```cpp
class TuningGrid {
public:
  virtual ~TuningGrid() = default;
  virtual float nearestCents(float centsModOctave) const = 0;
  virtual juce::String name() const = 0;
};
```
Concrete subclasses for v1+:
- `JustGrid5Limit` (default)
- `JustGrid7Limit`
- `EqualTemperament` (12-TET → identity for sanity test; 19-TET, 31-TET as bonuses)
- `Pythagorean`
- `QuarterCommaMeantone`
- `Werckmeister3`, `Kirnberger3`
- `Bohlen-Pierce` (no octave — needs a separate grid model)
- User-defined Scala (`.scl`) loader

Tonic estimator stays the same across grids — only the snap targets change.

## Tuning Reference Modes (parameter)

1. **Floating** (default): tonic estimated from audio, drifts.
2. **Anchored**: user pins tonic to a fixed pitch (e.g. A = 432 Hz, or C). No drift.
3. **MIDI-keyed** (v3): MIDI input provides current key/root; tonic locked to that.

## Phasing

### v0 — proof of life
- [x] **v0.1 Plugin scaffold** (done 2026-05-04). `Effects/AutoJust/` builds as Standalone + AU + VST3 universal binary; PGM GUI loads with stub `bypass` / `snapStrength` params; audio is identity-passthrough. No DSP yet.
- [x] **v0.2 STFT identity round-trip** (done 2026-05-04). `Source/Stft.{h,cpp}` — Hann² OLA, 4096/1024 (75% overlap), in-place per-channel streaming. `tests/StftRoundTripTest.cpp` runs sine, sweep, noise, impulse, DC; peak reconstruction error ≤ 6e-7 across all signals after latency alignment. Plugin uses it in `processBlock`; latency reported via `setLatencySamples(fftSize)`. Note: Hann² needs overlap ≥ 3 for COLA (overlap=2 leaves a residual cos(4πt/N) ripple); asserted in ctor.
- [x] **v0.3 Peak picker + diagnostics** (done 2026-05-04). `Source/PeakAnalyzer.{h,cpp}` extends `Stft`; per-frame: |X[k]| + atan2 phase, median-magnitude noise floor with `peakThresholdDb` + `absoluteFloor` gates, local-maximum picking, IF correction via consecutive-frame phase difference (`true_bin = k + wrap(dphi - 2πkH/N) · N/(2πH)`), top-N by magnitude. Latest peak set published as a thread-safe snapshot via `getResolvedPeaks()`. `tests/PeakAnalyzerTest.cpp`: pure tones at 110/440/442/5000 Hz all detected within 0.001 cents (442 Hz is between bins — IF correction doing real work); two-tone 440+660 both resolved; silence reports zero peaks. Plugin uses `PeakAnalyzer` in place of bare `Stft`; audio still identity-passthrough (analyzer's processSpectrum doesn't modify the spectrum yet).
- [x] **v0.4 Tonic estimator** (done 2026-05-04). `Source/TonicEstimator.{h,cpp}`: per-frame circular histogram of cents-mod-octave (default 120 bins → 10 cents/bin) with magnitude-weighted linear-interpolated deposit and exponential decay (default τ = 3 s). Mode estimated by argmax + parabolic interpolation; published tonic slewed toward the candidate at a configurable max drift rate (default 10 cents/sec, the rate the design doc identifies as the inaudibility ceiling). First valid frame snaps to candidate (no slew from default 0). Wired into `PeakAnalyzer` — analyzer holds the estimator, calls `update()` from `processSpectrum` on the report channel. `tests/TonicEstimatorTest.cpp`: steady tones at 440 Hz and 523.25 Hz settle to within 0.05 cents of the expected pitch class; drift cap of 10 cents/sec produces ≤ 1.21 cents over 0.5 s when the input shifts; silence leaves the tonic unchanged.

### v1 — minimal viable retuner
- [x] **v1a Single-peak retune (math sanity)** (done 2026-05-04). `Source/Retuner.{h,cpp}` extends `PeakAnalyzer`, overrides `onPeaksDetected` (new hook in `PeakAnalyzer` — old `processSpectrum` refactored into `detectAndCorrectPeaks` + hook). Strongest peak only, fixed test cents shift (no JI grid yet). Method: per-channel accumulator α += 2π·Δf·H/fs each hop; multiply every bin in the peak's region of influence by exp(jα). Because α is constant across the region within a frame and varies linearly across frames, OLA reconstructs an output multiplied by exp(j·2π·Δf·t) — i.e. shifted by Δf Hz. Identity phase locking (same phasor for all bins in region) preserves intra-region phase relations per Laroche–Dolson 1999 §V-C-1. `tests/RetunerTest.cpp`: ±5/20/30/50 cents shifts at 220/440/880/1000 Hz all reproduce target frequency at 0.000 cents error; disabled mode is bit-identical. Plugin holds a `Retuner` (default `enabled=false`) so default behavior is unchanged from v0.4.
- [x] **v1b All-peaks retune onto 5-limit JI grid** (done 2026-05-04). New `Source/TuningGrid.{h,cpp}` adds an abstract grid interface and `JustGrid5Limit` (12 standard 5-limit ratios as cents-mod-octave). `Retuner` extended: when `testShiftCents == 0`, every detected peak gets its own soft attractor toward the nearest grid point, anchored to `getTonicEstimator().getTonicCents()`. Pull strength scaled by `snapStrength` ([0..1], 0 = identity). Peaks farther than `maxBasinCents` (default 50) from the nearest grid point pass through unchanged. Region-of-influence carved at neighbor midpoints (capped by `regionHalfWidth`). Per-bin phase accumulators give frame-to-frame phase coherence. Plugin's `bypass` and `snapStrength` parameters now wired: `bypass=false → setEnabled(true)`, `snapStrength → setSnapStrength`. Tests: `tests/TuningGridTest.cpp` (10/10) verifies grid math including ET-→-JI deltas at known intervals (m3 → −13.69 cents, P5 → +1.96 cents, etc.) and circular wrap. `tests/RetunerJITest.cpp` (3/3) feeds a sustained ET dyad (C5 + ET-E5) through the Retuner with `snapStrength=1`: tonic settles to C pitch class (300.00 cents from A); the C peak is unchanged; the E peak shifts from 659.26 Hz (ET) to 654.07 Hz (JI 5/4) at 0.004 cents accuracy; with `snapStrength=0` the E stays at ET to within 0.004 cents.
- Test material: sustained string-quartet pad, organ chord progressions.

### v2 — harmonic grouping + multiple grids
- Group shifts (preserves inharmonicity).
- TuningGrid abstraction + ≥ 3 grid types.
- Anchored mode.

### v3 — production polish
- Scala file loader.
- Optional MIDI-side-chain key hint.
- GUI: tonic readout, drift trail, "what got snapped" visualization.
- Scope/quality knobs (snap strength, max drift rate, peak gate sensitivity).

## Real Pitfalls / Open Risks

| Risk | Mitigation |
|---|---|
| Latency too high for live monitoring | Ship as mixing plugin; document latency; v3 explore lower-latency front end (analytic-signal IF tracking?). |
| Vibrato gets flattened | IF-stability gate naturally rejects instantaneous vibrato peaks; verify on solo violin/voice. May need vibrato-aware mode that snaps the vibrato's *center* over a longer window. |
| Pitch flicker at grid-point boundaries | Hysteresis around each attractor; rate-limit shift output. |
| Inharmonic timbre flattening | Harmonic grouping (v2) is the fix; v1 will show the artifact on solo strings. |
| Comma drift becomes audible motion | Cap drift rate (≤ 1 cent / 100 ms); reset gently on long rests. |
| Phase-vocoder transient smearing | Standard problem — peak-locked approach helps; consider transient-detection bypass. |
| Two simultaneous keys (modulation moment, polychord) | Tonic estimator will average; acceptable for v1. v3: detect bimodal histogram and split the grid. |

## Validation Plan

- **Identity sanity**: 12-TET grid → output should be ≈ bit-identical to input (within phase-vocoder reconstruction tolerance). Unit test.
- **Pure-tone snap**: synthesize sine pair at 700 cents (ET fifth); expect output pair at 701.96 cents (3:2). Unit test.
- **String quartet**: render Bach chorale via ET sample library, A/B with AutoJust on. Listening test.
- **Solo violin with vibrato**: must pass through with vibrato intact. Listening test.
- **Drum bus**: must be a near-bypass. Listening test (and spectrogram diff).
- **Chord progression with intentional comma pump** (I–vi–ii–V–I in JI): grid should drift slightly over the cycle, no audible "swimming".

## Open Questions

1. STFT primitive: build fresh with JUCE's FFT, or reuse / extend something in `jos-modules/jos_dsp/`?
2. How aggressive should v1's default `max_pull` be? (Cautious: 3 cents/frame; bold: 10 cents/frame.)
3. Should the GUI show the inferred tonic and grid in real time? (Yes, eventually — great for trust-building during demos.)
4. Stereo handling: process M/S separately, or L/R independently with shared grid? (Probably shared grid, independent peak picking, to preserve stereo image.)
5. Sidechain input as "tuning reference" — interesting v3 feature: side-chain a clean piano track to lock the grid to the piano's tuning.

## Naming Conventions

- Plugin name: `AutoJust`
- Plugin company: `JOS` (matches existing instruments)
- Bundle id: `com.jos.AutoJust`
- Standalone target: `AutoJust_Standalone`; AU/VST3 targets follow the pattern from `Instruments/SingleCoil3/`.
- Makefile abbreviations: `aj` (e.g. `make ajr`, `make ajd`, `make ajro`).
