// Retuner — applies per-frame frequency shifts to spectral peaks via
// Laroche–Dolson identity-phase-locking-style differential phase rotation.
//
// v1a (this file): single strongest peak only, fixed cents shift configured
// from the outside (no JI grid yet). This is the math sanity check for v1b
// which will iterate over all peaks and snap each to the nearest 5-limit JI
// ratio relative to the moving tonic.
//
// Method: for each frame, accumulate a per-channel "extra rotation" α,
// incremented per hop by 2π·Δf·H/fs (where Δf is the desired shift in Hz).
// Multiply every bin in the peak's region of influence by exp(jα). Because α
// is constant *within* a frame and varies *linearly across* frames, the OLA-
// reconstructed time-domain signal is multiplied by exp(j·2π·Δf·t) in
// expectation — i.e. shifted in frequency by Δf Hz. Identity phase locking:
// applying the *same* phasor to all bins in the region preserves their
// internal phase relations, avoiding the "phasiness" artifact L-D 1999
// catalogues.
//
// Region of influence: ±regionHalfWidth bins around the peak. For an isolated
// peak windowed by Hann at N=4096, the main lobe is ~4 bins wide; default
// halfwidth=8 covers main lobe + first sidelobes safely.
//
// For shifts ≪ one bin (which is the regime AutoJust lives in — typical JI
// adjustments are <30 cents = <8 Hz at A4 = ~0.7 bins at fftSize=4096,
// fs=44100), pure phase rotation gives the correct perceived frequency without
// energy translation. v1b will revisit this when it has to handle multiple
// peaks whose regions might overlap.

#pragma once

#include "PeakAnalyzer.h"

#include <atomic>
#include <vector>

namespace autojust
{

class Retuner : public PeakAnalyzer
{
public:
    Retuner (int fftOrder = 12, int overlapFactor = 4);

    /** Master enable; when off the spectrum is identity. */
    void  setEnabled       (bool b)     noexcept { enabled.store (b, std::memory_order_relaxed); }
    bool  isEnabled        () const     noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Cents shift applied to the strongest peak's region of influence.
        Positive → up; negative → down. v1a uses this as a fixed test value. */
    void  setTestShiftCents (float c)   noexcept { testShiftCents.store (c, std::memory_order_relaxed); }
    float getTestShiftCents () const    noexcept { return testShiftCents.load (std::memory_order_relaxed); }

    /** Half-width (in bins) of the peak's region of influence. */
    void  setRegionHalfWidth (int w)    noexcept { regionHalfWidth = w; }

    /** Reset the per-channel phase accumulators (e.g. on transport stop). */
    void  resetPhaseAccumulators();

protected:
    void onPeaksDetected (float* data, int fftSize, int channel,
                          const std::vector<ResolvedPeak>& peaks) override;

private:
    std::atomic<bool>  enabled        { false };
    std::atomic<float> testShiftCents { 0.0f };
    int                regionHalfWidth { 8 };

    /** Per-channel accumulated phase rotation (radians) maintained across hops. */
    std::vector<float> phaseAccum;
};

} // namespace autojust
