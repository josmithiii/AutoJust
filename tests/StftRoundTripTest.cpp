// Standalone round-trip test for autojust::Stft.
// Builds as a console executable; pass criterion: peak reconstruction error
// below threshold for several test signals after accounting for latency.

#include "../Source/Stft.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

namespace
{

struct TestResult
{
    std::string name;
    float       peakError;
    float       rmsError;
    bool        passed;
};

float peakAbs (const float* x, int n)
{
    float p = 0.0f;
    for (int i = 0; i < n; ++i) p = std::max (p, std::abs (x[i]));
    return p;
}

float rmsAbs (const float* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * (double) x[i];
    return (float) std::sqrt (s / (double) n);
}

// Push `input` through `stft` in arbitrary block sizes; return the output.
std::vector<float> pushThrough (autojust::Stft& stft, const std::vector<float>& input, int blockSize)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    std::vector<float> out (input.size(), 0.0f);

    int written = 0;
    while (written < (int) input.size())
    {
        const int n = std::min (blockSize, (int) input.size() - written);
        buf.setSize (1, n, false, false, true);
        std::copy (input.begin() + written, input.begin() + written + n,
                   buf.getWritePointer (0));
        stft.process (buf);
        std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n,
                   out.begin() + written);
        written += n;
    }
    return out;
}

// Compute aligned error: out[t] should equal in[t - latency] for t >= latency.
TestResult checkRoundTrip (const std::string& name,
                           const std::vector<float>& in,
                           const std::vector<float>& out,
                           int latency,
                           float peakTol)
{
    const int N      = (int) in.size();
    const int settle = latency + 256;            // skip transient warmup
    const int len    = N - settle;
    if (len <= 0)
    {
        std::fprintf (stderr, "*** test '%s' input too short\n", name.c_str());
        return { name, 0.0f, 0.0f, false };
    }

    std::vector<float> err ((size_t) len);
    for (int i = 0; i < len; ++i)
        err[(size_t) i] = out[(size_t) (i + settle)] - in[(size_t) (i + settle - latency)];

    const float peak = peakAbs (err.data(), len);
    const float rms  = rmsAbs  (err.data(), len);
    const bool ok    = peak < peakTol;
    std::printf ("  %-22s peak=%.3e  rms=%.3e  %s\n",
                 name.c_str(), peak, rms, ok ? "PASS" : "*** FAIL");
    return { name, peak, rms, ok };
}

std::vector<float> sineSweep (int n, double sr, double f0, double f1)
{
    std::vector<float> x ((size_t) n);
    const double k = (f1 - f0) / (double) n;
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double f = f0 + k * (double) i;
        phase += 2.0 * M_PI * f / sr;
        x[(size_t) i] = (float) (0.5 * std::sin (phase));
    }
    return x;
}

std::vector<float> whiteNoise (int n, unsigned seed)
{
    std::vector<float> x ((size_t) n);
    juce::Random rng ((juce::int64) seed);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = rng.nextFloat() * 2.0f - 1.0f;
    return x;
}

std::vector<float> impulse (int n, int pos)
{
    std::vector<float> x ((size_t) n, 0.0f);
    if (pos >= 0 && pos < n) x[(size_t) pos] = 1.0f;
    return x;
}

} // namespace

int main()
{
    constexpr int   fftOrder      = 12;          // 4096 samples
    constexpr int   overlapFactor = 4;
    constexpr float peakTol       = 1.0e-4f;     // generous; Hann*Hann/1.5 OLA
                                                  // typically < 1e-5 in float
    constexpr double sampleRate   = 44100.0;
    constexpr int    testLength   = 32768;

    std::printf ("Stft round-trip test (fftSize=%d, hop=%d)\n",
                 1 << fftOrder, (1 << fftOrder) / overlapFactor);

    int passed = 0, total = 0;

    auto runOne = [&] (const std::string& name, const std::vector<float>& in, int blockSize)
    {
        autojust::Stft stft (fftOrder, overlapFactor);
        stft.prepare (1, blockSize);
        const int latency = stft.getLatencySamples();
        auto out = pushThrough (stft, in, blockSize);
        auto r = checkRoundTrip (name, in, out, latency, peakTol);
        if (r.passed) ++passed;
        ++total;
    };

    runOne ("sine 440 Hz, blk=512",
            sineSweep (testLength, sampleRate, 440.0, 440.0), 512);

    runOne ("sweep 100→8000 Hz, blk=64",
            sineSweep (testLength, sampleRate, 100.0, 8000.0), 64);

    runOne ("white noise, blk=1024",
            whiteNoise (testLength, 0xC0FFEEu), 1024);

    runOne ("impulse @ 5000, blk=128",
            impulse (testLength, 5000), 128);

    runOne ("DC, blk=256",
            std::vector<float> ((size_t) testLength, 0.7f), 256);

    std::printf ("\n%d / %d tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
