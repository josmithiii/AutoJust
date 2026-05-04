#include "TonicEstimator.h"

#include <cmath>
#include <algorithm>

namespace autojust
{

namespace
{
    inline float wrapToCircle (float c) noexcept
    {
        // Bring c into [0, 1200).
        c = std::fmod (c, 1200.0f);
        if (c < 0.0f) c += 1200.0f;
        return c;
    }

    inline float shortestSignedDelta (float target, float current) noexcept
    {
        float d = target - current;
        if (d >  600.0f) d -= 1200.0f;
        if (d < -600.0f) d += 1200.0f;
        return d;
    }
}

TonicEstimator::TonicEstimator (int numBinsIn)
    : numBins (numBinsIn)
{
    hist.assign ((size_t) numBins, 0.0f);
    recomputeRates();
}

void TonicEstimator::recomputeRates()
{
    if (sampleRate <= 0.0 || hopSize <= 0)
    {
        decayPerFrame      = 0.0f;
        driftPerFrameCents = 0.0f;
        return;
    }
    const float framesPerSec = (float) (sampleRate / (double) hopSize);
    decayPerFrame      = std::exp (-1.0f / (histTauSec * framesPerSec));
    driftPerFrameCents = maxDriftCps / framesPerSec;
}

void TonicEstimator::prepare (double sampleRateIn, int hopSizeIn)
{
    sampleRate = sampleRateIn;
    hopSize    = hopSizeIn;
    recomputeRates();
    reset();
}

void TonicEstimator::reset()
{
    std::lock_guard<std::mutex> lk (mu);
    std::fill (hist.begin(), hist.end(), 0.0f);
    currentTonicCents.store (0.0f, std::memory_order_relaxed);
    initialized = false;
}

std::vector<float> TonicEstimator::getHistogram() const
{
    std::lock_guard<std::mutex> lk (mu);
    return hist;
}

void TonicEstimator::update (const std::vector<ResolvedPeak>& peaks)
{
    float candidateCents = 0.0f;
    bool  haveSignal     = false;

    {
        std::lock_guard<std::mutex> lk (mu);

        // 1. Decay.
        for (auto& v : hist) v *= decayPerFrame;

        // 2. Deposit each peak with linear interpolation across two bins.
        const float binsPer1200 = (float) numBins / 1200.0f;
        for (const auto& p : peaks)
        {
            if (p.frequencyHz <= 0.0f) continue;
            const float cents = 1200.0f * std::log2 (p.frequencyHz / refFreqHz);
            const float cmo   = wrapToCircle (cents);
            const float bin   = cmo * binsPer1200;
            const int   ib    = (int) bin;
            const float frac  = bin - (float) ib;
            const int   i0    = ((ib    ) % numBins + numBins) % numBins;
            const int   i1    = ((ib + 1) % numBins + numBins) % numBins;
            hist[(size_t) i0] += p.magnitude * (1.0f - frac);
            hist[(size_t) i1] += p.magnitude * frac;
        }

        // 3. argmax.
        int   peakBin = 0;
        float peakMag = 0.0f;
        for (int i = 0; i < numBins; ++i)
            if (hist[(size_t) i] > peakMag) { peakMag = hist[(size_t) i]; peakBin = i; }

        // 4. Parabolic interp around argmax (circular).
        if (peakMag > 0.0f)
        {
            haveSignal = true;
            const float hm = hist[(size_t) ((peakBin + numBins - 1) % numBins)];
            const float hc = hist[(size_t) peakBin];
            const float hp = hist[(size_t) ((peakBin + 1) % numBins)];
            const float denom = hm - 2.0f * hc + hp;
            const float frac  = (std::abs (denom) > 1.0e-20f)
                                ? 0.5f * (hm - hp) / denom : 0.0f;
            const float fracClamped = std::max (-0.5f, std::min (0.5f, frac));
            const float interpBin = (float) peakBin + fracClamped;
            candidateCents = wrapToCircle (interpBin * 1200.0f / (float) numBins);
        }
    }

    if (! haveSignal) return;

    // 5. Slew toward candidate at rate-limited speed (circular shortest path).
    float current = currentTonicCents.load (std::memory_order_relaxed);
    float next;
    if (! initialized)
    {
        // First valid update — snap to candidate so we don't slew from arbitrary 0.
        next        = candidateCents;
        initialized = true;
    }
    else
    {
        const float delta    = shortestSignedDelta (candidateCents, current);
        const float maxStep  = driftPerFrameCents;
        const float clamped  = std::max (-maxStep, std::min (maxStep, delta));
        next = wrapToCircle (current + clamped);
    }
    currentTonicCents.store (next, std::memory_order_relaxed);
}

} // namespace autojust
