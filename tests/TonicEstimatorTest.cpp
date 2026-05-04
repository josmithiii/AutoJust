// Verifies TonicEstimator settles on the dominant pitch class and respects
// the rate-limited drift cap when the input pitch class changes.

#include "../Source/PeakAnalyzer.h"
#include "../Source/TonicEstimator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

constexpr int    fftOrder   = 12;
constexpr int    overlap    = 4;
constexpr double sampleRate = 44100.0;
constexpr int    blockSize  = 256;

float wrapCents (float c) noexcept
{
    c = std::fmod (c, 1200.0f);
    if (c < 0.0f) c += 1200.0f;
    return c;
}

float shortestDist (float a, float b) noexcept
{
    float d = std::abs (a - b);
    return std::min (d, 1200.0f - d);
}

float expectedCentsFor (float freqHz, float refHz = 440.0f) noexcept
{
    return wrapCents (1200.0f * std::log2 (freqHz / refHz));
}

void pushSine (autojust::PeakAnalyzer& a, double freq, int samples)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    const double k = 2.0 * M_PI * freq / sampleRate;
    int written = 0;
    static double phase = 0.0;
    while (written < samples)
    {
        const int n = std::min (blockSize, samples - written);
        buf.setSize (1, n, false, false, true);
        auto* w = buf.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            phase += k;
            w[i] = 0.5f * (float) std::sin (phase);
        }
        a.process (buf);
        written += n;
    }
}

struct Result { const char* name; bool ok; };

Result expectClose (const char* name, float measured, float target,
                    float maxCentsErr)
{
    const float err = shortestDist (measured, target);
    const bool  ok  = err < maxCentsErr;
    std::printf ("  %-44s tonic=%7.2f cents  target=%7.2f  err=%5.2f  %s\n",
                 name, measured, target, err, ok ? "PASS" : "*** FAIL");
    return { name, ok };
}

} // namespace

int main()
{
    int passed = 0, total = 0;
    auto run = [&] (Result r) { if (r.ok) ++passed; ++total; };

    std::printf ("TonicEstimator test\n");

    // 1. Steady sine at 440 Hz → tonic should converge to 0 cents (A pitch class).
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        // High drift cap so test settles fast.
        a.getTonicEstimator().setMaxDriftCpsPerSec (2400.0f);
        a.getTonicEstimator().setHistogramTauSec   (0.5f);
        pushSine (a, 440.0, (int) (sampleRate * 1.0)); // 1 second
        const float t = a.getTonicEstimator().getTonicCents();
        run (expectClose ("steady 440 Hz", t, 0.0f, 15.0f));
    }

    // 2. Steady C5 (≈523.25 Hz) → tonic should converge to C pitch class
    //    (referenced to A4 = 440: cents = 300).
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        a.getTonicEstimator().setMaxDriftCpsPerSec (2400.0f);
        a.getTonicEstimator().setHistogramTauSec   (0.5f);
        pushSine (a, 523.2511, (int) (sampleRate * 1.0));
        const float t        = a.getTonicEstimator().getTonicCents();
        const float expected = expectedCentsFor (523.2511f);
        run (expectClose ("steady C5 (523.25 Hz)", t, expected, 15.0f));
    }

    // 3. Drift cap honored: switch from A4 to C5 with cap=10 cents/sec, run for
    //    only 0.5 sec; tonic should have moved <= ~5 cents from initial.
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        a.getTonicEstimator().setMaxDriftCpsPerSec (2400.0f);
        a.getTonicEstimator().setHistogramTauSec   (0.5f);
        // Settle on A.
        pushSine (a, 440.0, (int) (sampleRate * 1.0));
        const float t0 = a.getTonicEstimator().getTonicCents();

        // Now lower the drift cap and run a different pitch.
        a.getTonicEstimator().setMaxDriftCpsPerSec (10.0f); // 10 cents/sec
        pushSine (a, 523.2511, (int) (sampleRate * 0.5));    // 0.5 sec
        const float t1   = a.getTonicEstimator().getTonicCents();
        const float drift = shortestDist (t1, t0);
        const bool   ok   = drift <= 6.0f; // expect ≤ 5, allow margin
        std::printf ("  %-44s drift=%5.2f cents over 0.5s  %s\n",
                     "drift cap @ 10 cents/sec, 0.5s window",
                     drift, ok ? "PASS" : "*** FAIL");
        run ({ "drift-cap", ok });
    }

    // 4. Silence → tonic shouldn't move.
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        a.getTonicEstimator().setMaxDriftCpsPerSec (2400.0f);
        a.getTonicEstimator().setHistogramTauSec   (0.5f);
        // Push silence — no peaks, no histogram updates, tonic stays at 0
        // (uninitialized default).
        std::vector<float> z ((size_t) (sampleRate * 0.5), 0.0f);
        juce::AudioBuffer<float> buf (1, blockSize);
        int written = 0;
        while (written < (int) z.size())
        {
            const int n = std::min (blockSize, (int) z.size() - written);
            buf.setSize (1, n, false, false, true);
            buf.clear();
            a.process (buf);
            written += n;
        }
        const float t = a.getTonicEstimator().getTonicCents();
        run (expectClose ("silence (tonic stays at 0)", t, 0.0f, 0.001f));
    }

    std::printf ("\n%d / %d tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
