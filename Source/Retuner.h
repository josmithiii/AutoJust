// Retuner — applies per-frame frequency shifts to spectral peaks via
// Laroche–Dolson identity-phase-locking-style differential phase rotation.
//
// Two modes, switched by `testShiftCents`:
//   - testShiftCents != 0 → MANUAL: shift only the strongest peak by the
//     configured amount (this is the v1a math sanity path).
//   - testShiftCents == 0 → JUST: every peak gets its own soft attractor
//     toward the nearest grid point of the held TuningGrid (default
//     JustGrid5Limit), referenced to the tonic estimator's current
//     cents-mod-octave reading. Pull strength scaled by `snapStrength`
//     [0..1]; zero = no retune; one = full pull.
//
// Method (per peak, both modes):
//   α[bin] += 2π · Δf · H / fs   each hop
//   for k in region-of-influence(peak): bin[k] *= exp(jα[bin])
//
// The per-bin accumulator α[bin] preserves frame-to-frame phase coherence at
// the new frequency (constant within a frame, linear across frames →
// reconstructed as exp(j·2π·Δf·t) in the time domain). Identity-phase locking
// via the same phasor across all bins in the region preserves intra-region
// phase relations (Laroche–Dolson 1999 §V-C-1), avoiding the "phasiness"
// artifact catalogued there.
//
// Region of influence: carved at the midpoint to each neighboring peak (so
// adjacent peaks don't fight over shared bins). Outermost peaks extend to DC
// or Nyquist edge respectively.
//
// Soft attractor: peaks farther than `maxBasinCents` from the nearest grid
// point are passed through unchanged (no snap). Inside the basin, the pull
// is `Δ_grid · snapStrength` — linear, no saturation curve.

#pragma once

#include "PeakAnalyzer.h"
#include "TuningGrid.h"

#include <atomic>
#include <memory>
#include <vector>

namespace autojust
{

class Retuner : public PeakAnalyzer
{
public:
    Retuner (int fftOrder = 12, int overlapFactor = 4);

    /** Master enable; when off the spectrum is identity. */
    void  setEnabled        (bool b)  noexcept { enabled.store (b, std::memory_order_relaxed); }
    bool  isEnabled         () const  noexcept { return enabled.load (std::memory_order_relaxed); }

    /** v1a-style manual shift on strongest peak. Nonzero overrides JI mode. */
    void  setTestShiftCents (float c) noexcept { testShiftCents.store (c, std::memory_order_relaxed); }
    float getTestShiftCents () const  noexcept { return testShiftCents.load (std::memory_order_relaxed); }

    /** JI-mode pull strength: 0 = no snap, 1 = full snap to grid point. */
    void  setSnapStrength   (float s) noexcept { snapStrength.store (s, std::memory_order_relaxed); }
    float getSnapStrength   () const  noexcept { return snapStrength.load (std::memory_order_relaxed); }

    /** Half-width (in bins) of the peak's region of influence (used in
        manual mode and as a cap in JI mode). JI mode otherwise uses the
        midpoint-to-neighbor carving. */
    void  setRegionHalfWidth (int w)  noexcept { regionHalfWidth = w; }

    /** Peaks farther than this many cents from the nearest grid point are
        not snapped (passed through unchanged). Default 50 cents. */
    void  setMaxBasinCents   (float c) noexcept { maxBasinCents.store (c, std::memory_order_relaxed); }

    /** Swap in a different tuning grid (e.g. Pythagorean, ET) for v2. */
    void  setTuningGrid (std::unique_ptr<TuningGrid> g) noexcept { grid = std::move (g); }
    const TuningGrid& getTuningGrid() const noexcept { return *grid; }

    /** Reset all per-bin / per-channel phase accumulators. */
    void  resetPhaseAccumulators();

protected:
    void onPeaksDetected (float* data, int fftSize, int channel,
                          const std::vector<ResolvedPeak>& peaks) override;

private:
    void applyManualMode (float* data, int fftSize, int channel,
                          const std::vector<ResolvedPeak>& peaks);
    void applyJustMode   (float* data, int fftSize, int channel,
                          const std::vector<ResolvedPeak>& peaks);

    void rotateRegion (float* data, int kMin, int kMax,
                       float cosA, float sinA);

    std::atomic<bool>  enabled        { false };
    std::atomic<float> testShiftCents { 0.0f };
    std::atomic<float> snapStrength   { 1.0f };
    std::atomic<float> maxBasinCents  { 50.0f };
    int                regionHalfWidth { 8 };

    std::unique_ptr<TuningGrid> grid;

    /** Manual-mode (v1a): one accumulator per channel. */
    std::vector<float> manualAccum;

    /** JI-mode: per-channel, per-bin accumulator. Keyed by current frame's
        peak bin location — peaks that drift across bins lose continuity at
        the migration boundary, but for stable peaks this is exact. */
    std::vector<std::vector<float>> binAccum;
};

} // namespace autojust
