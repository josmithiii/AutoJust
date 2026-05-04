// Verifies Retuner shifts the strongest peak by the configured cents amount,
// using Laroche–Dolson-style differential phase rotation in the peak's
// region of influence.
//
// Strategy: synthesize a pure sine, push through Retuner with a known cents
// shift, then push the OUTPUT through a fresh PeakAnalyzer and check that the
// detected peak frequency matches the target (input · 2^(cents/1200)) within
// a small cents tolerance.

#include "../Source/Retuner.h"
#include "../Source/PeakAnalyzer.h"

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

std::vector<float> sine (int n, double freq, float amp = 0.5f)
{
    std::vector<float> x ((size_t) n);
    const double k = 2.0 * M_PI * freq / sampleRate;
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = amp * (float) std::sin (k * (double) i);
    return x;
}

std::vector<float> processThrough (autojust::Retuner& r,
                                   const std::vector<float>& in)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    std::vector<float> out (in.size(), 0.0f);
    int written = 0;
    while (written < (int) in.size())
    {
        const int n = std::min (blockSize, (int) in.size() - written);
        buf.setSize (1, n, false, false, true);
        std::copy (in.begin() + written, in.begin() + written + n,
                   buf.getWritePointer (0));
        r.process (buf);
        std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n,
                   out.begin() + written);
        written += n;
    }
    return out;
}

void analyze (autojust::PeakAnalyzer& a, const std::vector<float>& signal)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    int written = 0;
    while (written < (int) signal.size())
    {
        const int n = std::min (blockSize, (int) signal.size() - written);
        buf.setSize (1, n, false, false, true);
        std::copy (signal.begin() + written, signal.begin() + written + n,
                   buf.getWritePointer (0));
        a.process (buf);
        written += n;
    }
}

float detectStrongestHz (const std::vector<float>& signal,
                         double sr, int latencyToSkip)
{
    autojust::PeakAnalyzer a (fftOrder, overlap);
    a.prepare (sr, 1, blockSize);

    // Skip the front portion: contains the round-trip warm-up zeros from the
    // Retuner's STFT latency, which would dominate as silence.
    std::vector<float> trimmed (signal.begin() + std::min ((int) signal.size(), latencyToSkip),
                                signal.end());
    analyze (a, trimmed);

    auto peaks = a.getResolvedPeaks();
    if (peaks.empty()) return -1.0f;
    const autojust::ResolvedPeak* best = &peaks.front();
    for (const auto& p : peaks)
        if (p.magnitude > best->magnitude) best = &p;
    return best->frequencyHz;
}

struct Result { const char* name; bool ok; };

Result runShiftTest (const char* name,
                     double inputHz,
                     float  shiftCents,
                     float  centsTol)
{
    autojust::Retuner r (fftOrder, overlap);
    r.prepare (sampleRate, 1, blockSize);
    r.setEnabled (true);
    r.setTestShiftCents (shiftCents);

    constexpr int N = (int) (sampleRate * 1.0); // 1 second
    auto in  = sine (N, inputHz);
    auto out = processThrough (r, in);

    // Skip Retuner's STFT latency from the output before analyzing.
    const int latency = r.getLatencySamples();
    const float detected = detectStrongestHz (out, sampleRate, latency);
    const float target   = inputHz * std::pow (2.0f, shiftCents / 1200.0f);
    const float err      = (detected > 0.0f)
                           ? 1200.0f * std::log2 (detected / target)
                           : 1.0e6f;
    const bool  ok       = std::abs (err) < centsTol;
    std::printf ("  %-44s in=%.1f  shift=%+5.1fc  out=%9.4f Hz  target=%9.4f  err=%+6.3fc  %s\n",
                 name, inputHz, shiftCents, detected, target, err,
                 ok ? "PASS" : "*** FAIL");
    return { name, ok };
}

} // namespace

int main()
{
    int passed = 0, total = 0;
    auto run = [&] (Result r) { if (r.ok) ++passed; ++total; };

    std::printf ("Retuner test (single-peak Laroche–Dolson differential phase rotation)\n");

    // Small shifts (<1 bin at fftSize=4096, fs=44100 → ~10.8 Hz/bin):
    run (runShiftTest ("440 Hz, +30 cents",  440.0,  +30.0f, 1.5f));
    run (runShiftTest ("440 Hz, -30 cents",  440.0,  -30.0f, 1.5f));
    run (runShiftTest ("440 Hz, +5 cents",   440.0,   +5.0f, 1.5f));
    run (runShiftTest ("440 Hz, -50 cents",  440.0,  -50.0f, 2.0f));
    run (runShiftTest ("220 Hz, +20 cents",  220.0,  +20.0f, 1.5f));
    run (runShiftTest ("880 Hz, +20 cents",  880.0,  +20.0f, 1.5f));

    // Disabled: must be identity.
    {
        autojust::Retuner r (fftOrder, overlap);
        r.prepare (sampleRate, 1, blockSize);
        r.setEnabled (false);
        r.setTestShiftCents (+30.0f); // ignored

        constexpr int N = (int) (sampleRate * 1.0);
        auto in  = sine (N, 440.0);
        auto out = processThrough (r, in);
        const float detected = detectStrongestHz (out, sampleRate, r.getLatencySamples());
        const float err      = 1200.0f * std::log2 (detected / 440.0f);
        const bool  ok       = std::abs (err) < 0.5f;
        std::printf ("  %-44s detected=%9.4f Hz  err=%+6.3fc  %s\n",
                     "disabled (identity passthrough)", detected, err,
                     ok ? "PASS" : "*** FAIL");
        run ({ "disabled-identity", ok });
    }

    std::printf ("\n%d / %d tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
