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
- [ ] **v0.2 STFT identity round-trip.** Add `Source/Stft.{h,cpp}`. Hann analysis + synthesis windows, 75% overlap, 4096-sample frame. Plugin still passes audio through, but now via STFT → ISTFT round-trip. Verify reconstruction error is at machine epsilon over sine sweeps + white noise. Unit test in `tests/`.
- [ ] **v0.3 Peak picker + diagnostics.** Add peak detection with IF estimation, log resolved peaks; no retuning yet.
- [ ] **v0.4 Tonic estimator.** Histogram + LPF; log moving tonic.

### v1 — minimal viable retuner
- Per-peak (no grouping yet) soft-attractor toward 5-limit JI grid.
- Floating tonic with drift limit.
- A/B bypass; latency reporting.
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
