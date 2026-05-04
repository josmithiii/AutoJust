#include "Retuner.h"

#include <cmath>
#include <algorithm>

namespace autojust
{

namespace
{
    constexpr float kTwoPi = 6.2831853071795864769f;
    constexpr float kPi    = 3.1415926535897932384f;

    inline float wrapToPi (float x) noexcept
    {
        x = std::fmod (x + kPi, kTwoPi);
        if (x < 0.0f) x += kTwoPi;
        return x - kPi;
    }
}

Retuner::Retuner (int fftOrderIn, int overlapFactorIn)
    : PeakAnalyzer (fftOrderIn, overlapFactorIn)
{
    // phaseAccum sized lazily on first frame per channel (we don't know
    // numChannels until prepare(); subclasses can't easily hook prepare in
    // PeakAnalyzer without virtualizing it). Kept simple.
}

void Retuner::resetPhaseAccumulators()
{
    std::fill (phaseAccum.begin(), phaseAccum.end(), 0.0f);
}

void Retuner::onPeaksDetected (float* data, int fftSize, int channel,
                               const std::vector<ResolvedPeak>& peaks)
{
    if (! enabled.load (std::memory_order_relaxed)) return;
    if (peaks.empty()) return;

    if ((int) phaseAccum.size() <= channel)
        phaseAccum.resize ((size_t) channel + 1, 0.0f);

    // Strongest peak (v1a: just one).
    const ResolvedPeak* strongest = &peaks.front();
    for (const auto& p : peaks)
        if (p.magnitude > strongest->magnitude) strongest = &p;

    const float shiftCents = testShiftCents.load (std::memory_order_relaxed);
    if (std::abs (shiftCents) < 0.001f) return;

    // Δf in Hz: f' - f = f · (2^(c/1200) - 1).
    const float shiftFactor = std::pow (2.0f, shiftCents / 1200.0f);
    const float deltaHz     = strongest->frequencyHz * (shiftFactor - 1.0f);

    // Phase increment per hop = 2π · Δf · H / fs.
    const double sr = getSampleRate();
    const float  H  = (float) getHopSize();
    const float  dPhi = (float) (kTwoPi * (double) deltaHz * (double) H / sr);

    float& accum = phaseAccum[(size_t) channel];
    accum = wrapToPi (accum + dPhi);

    const float cosA = std::cos (accum);
    const float sinA = std::sin (accum);

    // Apply rotation to region of influence (skip DC and Nyquist).
    const int kp     = strongest->bin;
    const int W      = regionHalfWidth;
    const int kMin   = std::max (1, kp - W);
    const int kMax   = std::min (fftSize / 2 - 1, kp + W);
    for (int k = kMin; k <= kMax; ++k)
    {
        const float re = data[2 * k];
        const float im = data[2 * k + 1];
        data[2 * k    ] = cosA * re - sinA * im;
        data[2 * k + 1] = sinA * re + cosA * im;
    }
}

} // namespace autojust
