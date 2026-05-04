#include "Retuner.h"
#include "TonicEstimator.h"

#include <cmath>
#include <algorithm>

namespace autojust
{

namespace
{
    constexpr float kTwoPi = 6.2831853071795864769f;
    constexpr float kPi    = 3.1415926535897932384f;
    constexpr float kRefFreqHz = 440.0f; // tonic estimator's reference

    inline float wrapToPi (float x) noexcept
    {
        x = std::fmod (x + kPi, kTwoPi);
        if (x < 0.0f) x += kTwoPi;
        return x - kPi;
    }
}

Retuner::Retuner (int fftOrderIn, int overlapFactorIn)
    : PeakAnalyzer (fftOrderIn, overlapFactorIn),
      grid (std::make_unique<JustGrid5Limit>())
{
}

void Retuner::resetPhaseAccumulators()
{
    std::fill (manualAccum.begin(), manualAccum.end(), 0.0f);
    for (auto& ch : binAccum)
        std::fill (ch.begin(), ch.end(), 0.0f);
}

void Retuner::rotateRegion (float* data, int kMin, int kMax,
                            float cosA, float sinA)
{
    for (int k = kMin; k <= kMax; ++k)
    {
        const float re = data[2 * k];
        const float im = data[2 * k + 1];
        data[2 * k    ] = cosA * re - sinA * im;
        data[2 * k + 1] = sinA * re + cosA * im;
    }
}

void Retuner::onPeaksDetected (float* data, int fftSize, int channel,
                               const std::vector<ResolvedPeak>& peaks)
{
    if (! enabled.load (std::memory_order_relaxed)) return;
    if (peaks.empty()) return;

    if (std::abs (testShiftCents.load (std::memory_order_relaxed)) > 0.001f)
        applyManualMode (data, fftSize, channel, peaks);
    else
        applyJustMode (data, fftSize, channel, peaks);
}

void Retuner::applyManualMode (float* data, int fftSize, int channel,
                               const std::vector<ResolvedPeak>& peaks)
{
    if ((int) manualAccum.size() <= channel)
        manualAccum.resize ((size_t) channel + 1, 0.0f);

    const ResolvedPeak* strongest = &peaks.front();
    for (const auto& p : peaks)
        if (p.magnitude > strongest->magnitude) strongest = &p;

    const float shiftCents = testShiftCents.load (std::memory_order_relaxed);
    const float shiftFactor = std::pow (2.0f, shiftCents / 1200.0f);
    const float deltaHz     = strongest->frequencyHz * (shiftFactor - 1.0f);

    const double sr   = getSampleRate();
    const float  H    = (float) getHopSize();
    const float  dPhi = (float) (kTwoPi * (double) deltaHz * (double) H / sr);

    float& accum = manualAccum[(size_t) channel];
    accum = wrapToPi (accum + dPhi);

    const int kp   = strongest->bin;
    const int W    = regionHalfWidth;
    const int kMin = std::max (1, kp - W);
    const int kMax = std::min (fftSize / 2 - 1, kp + W);
    rotateRegion (data, kMin, kMax, std::cos (accum), std::sin (accum));
}

void Retuner::applyJustMode (float* data, int fftSize, int channel,
                             const std::vector<ResolvedPeak>& peaks)
{
    const int nBins = fftSize / 2 + 1;

    if ((int) binAccum.size() <= channel)
        binAccum.resize ((size_t) channel + 1);
    auto& accumPerBin = binAccum[(size_t) channel];
    if ((int) accumPerBin.size() != nBins)
        accumPerBin.assign ((size_t) nBins, 0.0f);

    // Sort peaks by bin so neighbor relationships work for region carving.
    std::vector<ResolvedPeak> sorted = peaks;
    std::sort (sorted.begin(), sorted.end(),
               [] (const ResolvedPeak& a, const ResolvedPeak& b) { return a.bin < b.bin; });

    const float tonicCents = getTonicEstimator().getTonicCents();
    const float pull       = snapStrength.load (std::memory_order_relaxed);
    const float maxBasin   = maxBasinCents.load (std::memory_order_relaxed);
    const double sr        = getSampleRate();
    const float  H         = (float) getHopSize();
    const float  hopOverFs = H / (float) sr;

    const int N = (int) sorted.size();
    for (int i = 0; i < N; ++i)
    {
        const ResolvedPeak& p = sorted[(size_t) i];
        if (p.frequencyHz <= 0.0f || p.bin < 1 || p.bin >= nBins - 1) continue;

        // Peak's pitch class relative to tonic (cents-mod-octave with tonic at 0).
        const float absCents = 1200.0f * std::log2 (p.frequencyHz / kRefFreqHz);
        float       relCmo   = std::fmod (absCents - tonicCents, 1200.0f);
        if (relCmo < 0.0f) relCmo += 1200.0f;

        // Soft attractor: zero outside basin, scaled inside.
        const float gridDelta = grid->nearestDeltaCents (relCmo);
        if (std::abs (gridDelta) > maxBasin) continue;
        const float deltaCents = gridDelta * pull;
        if (std::abs (deltaCents) < 0.001f) continue;

        // Δf in Hz, then per-hop phase increment.
        const float shiftFactor = std::pow (2.0f, deltaCents / 1200.0f);
        const float deltaHz     = p.frequencyHz * (shiftFactor - 1.0f);
        const float dPhi        = kTwoPi * deltaHz * hopOverFs;

        float& accum = accumPerBin[(size_t) p.bin];
        accum = wrapToPi (accum + dPhi);
        const float cosA = std::cos (accum);
        const float sinA = std::sin (accum);

        // Region of influence: midpoint to each neighbor (bin-wise), clamped.
        const int kPrev = (i == 0)         ? 0              : sorted[(size_t) (i - 1)].bin;
        const int kNext = (i == N - 1)     ? (nBins - 1)    : sorted[(size_t) (i + 1)].bin;
        int kMin = std::max (1,             (kPrev + p.bin + 1) / 2);
        int kMax = std::min (fftSize/2 - 1, (p.bin + kNext) / 2);

        // Cap by regionHalfWidth so a lone peak doesn't grab the whole spectrum.
        kMin = std::max (kMin, p.bin - regionHalfWidth);
        kMax = std::min (kMax, p.bin + regionHalfWidth);

        rotateRegion (data, kMin, kMax, cosA, sinA);
    }
}

} // namespace autojust
