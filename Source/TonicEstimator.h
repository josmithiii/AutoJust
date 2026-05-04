// Tonic / tonal-center estimator.
// Consumes a stream of resolved peaks (from PeakAnalyzer) and maintains a
// slowly-drifting estimate of the dominant pitch class (cents modulo 1200,
// referenced to a configurable pitch — A4 = 440 Hz by default, so 0 cents
// means the A pitch class).
//
// Method:
//   1. Per frame, decay a circular histogram of cents-mod-octave (one bin per
//      `centsPerBin` cents); deposit each peak's magnitude at its corresponding
//      bin (linear-interpolated across the two adjacent bins for sub-bin
//      precision).
//   2. argmax + parabolic interpolation gives the candidate tonic.
//   3. Rate-limit the published tonic (slew toward candidate at ≤ maxDriftCps
//      cents per second, with circular shortest-path wrapping).
//
// The histogram decay (τ ≈ histTauSec) gives the long-time integration; the
// drift cap prevents audible "swimming" when v1 starts using the tonic to
// retune. Cap is intentionally low (≈ 10 cents/sec) for v1 use; tests can
// raise it via setMaxDriftCpsPerSec to verify behavior quickly.

#pragma once

#include "PeakAnalyzer.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace autojust
{

class TonicEstimator
{
public:
    explicit TonicEstimator (int numBins = 120); // 1200 / 120 = 10 cents/bin

    void prepare (double sampleRate, int hopSize);
    void reset();

    /** Update histogram and slew-limit the published tonic. Call once per
        STFT frame (audio thread). */
    void update (const std::vector<ResolvedPeak>& peaks);

    /** Most recent tonic in cents-mod-octave [0, 1200). Lock-free. */
    float getTonicCents() const noexcept { return currentTonicCents.load (std::memory_order_relaxed); }

    /** Histogram snapshot for visualization. Thread-safe. */
    std::vector<float> getHistogram() const;

    void setReferenceFreqHz   (float hz) noexcept { refFreqHz   = hz; }
    void setHistogramTauSec   (float s)  noexcept { histTauSec  = s; recomputeRates(); }
    void setMaxDriftCpsPerSec (float c)  noexcept { maxDriftCps = c; recomputeRates(); }

    int  getNumBins() const noexcept { return numBins; }

private:
    void recomputeRates();

    int    numBins;
    float  refFreqHz   = 440.0f;
    float  histTauSec  = 3.0f;
    float  maxDriftCps = 10.0f;          // cents per second

    double sampleRate         = 44100.0;
    int    hopSize            = 1024;
    float  decayPerFrame      = 0.0f;
    float  driftPerFrameCents = 0.0f;

    mutable std::mutex mu;
    std::vector<float> hist;

    std::atomic<float> currentTonicCents { 0.0f };
    bool initialized = false;            // first valid update seeds the slew
};

} // namespace autojust
