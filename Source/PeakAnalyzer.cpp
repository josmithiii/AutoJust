#include "PeakAnalyzer.h"
#include "TonicEstimator.h"

#include <cmath>
#include <algorithm>

namespace autojust
{

namespace
{
    constexpr float kTwoPi = 6.2831853071795864769f;

    inline float wrapToPi (float x) noexcept
    {
        // Wrap into (-π, π].
        x = std::fmod (x + 3.1415926535897932384f, kTwoPi);
        if (x < 0.0f) x += kTwoPi;
        return x - 3.1415926535897932384f;
    }
}

PeakAnalyzer::PeakAnalyzer (int fftOrderIn, int overlapFactorIn)
    : Stft (fftOrderIn, overlapFactorIn),
      tonic (std::make_unique<TonicEstimator>())
{
    numBins = getFftSize() / 2 + 1;
}

PeakAnalyzer::~PeakAnalyzer() = default;

void PeakAnalyzer::prepare (double sampleRateIn, int numChannels, int maxBlockSize)
{
    Stft::prepare (numChannels, maxBlockSize);
    sampleRate = sampleRateIn;
    numBins    = getFftSize() / 2 + 1;

    prevPhase.assign ((size_t) numChannels, std::vector<float> ((size_t) numBins, 0.0f));

    {
        std::lock_guard<std::mutex> lk (peaksMutex);
        latestPeaks.clear();
    }

    tonic->prepare (sampleRateIn, getHopSize());
}

void PeakAnalyzer::reset()
{
    Stft::reset();
    for (auto& ch : prevPhase)
        std::fill (ch.begin(), ch.end(), 0.0f);

    {
        std::lock_guard<std::mutex> lk (peaksMutex);
        latestPeaks.clear();
    }

    tonic->reset();
}

std::vector<ResolvedPeak> PeakAnalyzer::getResolvedPeaks() const
{
    std::lock_guard<std::mutex> lk (peaksMutex);
    return latestPeaks;
}

std::vector<ResolvedPeak> PeakAnalyzer::detectAndCorrectPeaks (float* data, int fftSize, int channel)
{
    if (channel < 0 || channel >= (int) prevPhase.size())
        return {};

    const int   N        = fftSize;
    const int   hop      = getHopSize();
    const int   nBins    = N / 2 + 1;
    const float binToHz  = (float) (sampleRate / (double) N);
    const float expCoeff = kTwoPi * (float) hop / (float) N;

    auto& prev = prevPhase[(size_t) channel];

    std::vector<float> mag ((size_t) nBins);
    std::vector<float> phase ((size_t) nBins);
    for (int k = 0; k < nBins; ++k)
    {
        const float re = data[2 * k];
        const float im = data[2 * k + 1];
        mag[(size_t) k]   = std::sqrt (re * re + im * im);
        phase[(size_t) k] = std::atan2 (im, re);
    }

    std::vector<float> sorted = mag;
    std::nth_element (sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const float median = sorted[sorted.size() / 2];
    const float ratio  = std::pow (10.0f, peakThresholdDb / 20.0f);
    const float thr    = std::max (median * ratio, absoluteFloor);

    std::vector<ResolvedPeak> peaks;
    peaks.reserve ((size_t) maxPeaks);

    for (int k = 1; k < nBins - 1; ++k)
    {
        const float m = mag[(size_t) k];
        if (m <= thr) continue;
        if (m <= mag[(size_t) (k - 1)] || m <= mag[(size_t) (k + 1)]) continue;

        const float dphi      = phase[(size_t) k] - prev[(size_t) k];
        const float expected  = expCoeff * (float) k;
        const float princDphi = wrapToPi (dphi - expected);
        const float trueBin   = (float) k + princDphi / expCoeff;

        ResolvedPeak p;
        p.frequencyHz = trueBin * binToHz;
        p.magnitude   = m;
        p.bin         = k;
        peaks.push_back (p);
    }

    if ((int) peaks.size() > maxPeaks)
    {
        std::partial_sort (peaks.begin(), peaks.begin() + maxPeaks, peaks.end(),
                           [] (const ResolvedPeak& a, const ResolvedPeak& b)
                           { return a.magnitude > b.magnitude; });
        peaks.resize ((size_t) maxPeaks);
    }

    std::copy (phase.begin(), phase.end(), prev.begin());
    return peaks;
}

void PeakAnalyzer::processSpectrum (float* data, int fftSize, int channel)
{
    auto peaks = detectAndCorrectPeaks (data, fftSize, channel);

    // Subclass hook — may modify the spectrum in place.
    onPeaksDetected (data, fftSize, channel, peaks);

    if (channel == reportChannel)
    {
        tonic->update (peaks);

        std::unique_lock<std::mutex> lk (peaksMutex, std::try_to_lock);
        if (lk.owns_lock())
            latestPeaks = std::move (peaks);
    }
}

} // namespace autojust
