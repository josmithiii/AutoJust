// Verifies PeakAnalyzer's IF-corrected frequency detection.
// Pass criterion: detected frequency within 1 cent of synthesized tone for
// pure tones and the strongest peak of a two-tone signal.

#include "../Source/PeakAnalyzer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

float centsBetween (float measured, float reference) noexcept
{
    return 1200.0f * std::log2 (measured / reference);
}

void pushSamples (autojust::PeakAnalyzer& a,
                  const std::vector<float>& mono,
                  int blockSize)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    int written = 0;
    while (written < (int) mono.size())
    {
        const int n = std::min (blockSize, (int) mono.size() - written);
        buf.setSize (1, n, false, false, true);
        std::copy (mono.begin() + written, mono.begin() + written + n,
                   buf.getWritePointer (0));
        a.process (buf);
        written += n;
    }
}

std::vector<float> sine (int n, double sr, double freq, float amp = 0.5f)
{
    std::vector<float> x ((size_t) n);
    const double k = 2.0 * M_PI * freq / sr;
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = amp * (float) std::sin (k * (double) i);
    return x;
}

std::vector<float> sineSum (int n, double sr,
                            double f1, float a1,
                            double f2, float a2)
{
    auto x = sine (n, sr, f1, a1);
    auto y = sine (n, sr, f2, a2);
    for (int i = 0; i < n; ++i) x[(size_t) i] += y[(size_t) i];
    return x;
}

const autojust::ResolvedPeak* nearestPeak (const std::vector<autojust::ResolvedPeak>& peaks,
                                           float targetHz)
{
    const autojust::ResolvedPeak* best = nullptr;
    float bestErr = 1.0e30f;
    for (auto& p : peaks)
    {
        const float err = std::abs (p.frequencyHz - targetHz);
        if (err < bestErr) { bestErr = err; best = &p; }
    }
    return best;
}

struct Result { const char* name; bool passed; };

Result expectFrequency (const char* name,
                        autojust::PeakAnalyzer& a,
                        float targetHz,
                        float maxCentsError)
{
    auto peaks = a.getResolvedPeaks();
    auto* p = nearestPeak (peaks, targetHz);
    if (p == nullptr)
    {
        std::printf ("  %-44s no peak near %.2f Hz  *** FAIL\n", name, targetHz);
        return { name, false };
    }
    const float err = centsBetween (p->frequencyHz, targetHz);
    const bool ok   = std::abs (err) < maxCentsError;
    std::printf ("  %-44s detected=%9.4f Hz  err=%+7.3f cents  %s\n",
                 name, p->frequencyHz, err, ok ? "PASS" : "*** FAIL");
    return { name, ok };
}

} // namespace

int main()
{
    constexpr int    fftOrder      = 12;
    constexpr int    overlap       = 4;
    constexpr double sampleRate    = 44100.0;
    constexpr int    blockSize     = 256;
    constexpr int    numFrames     = 16;
    constexpr int    runLength     = (1 << fftOrder) * numFrames;
    constexpr float  centsTolPure  = 1.0f;   // 1 cent for pure tones

    std::printf ("PeakAnalyzer test (fftSize=%d, hop=%d, sr=%.0f)\n",
                 1 << fftOrder, (1 << fftOrder) / overlap, sampleRate);

    int passed = 0, total = 0;
    auto run = [&] (Result r) { if (r.passed) ++passed; ++total; };

    // 1. Pure tone exactly on a bin (440 Hz is between bins; bin 41 ≈ 441.5 Hz).
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        pushSamples (a, sine (runLength, sampleRate, 440.0), blockSize);
        run (expectFrequency ("pure 440 Hz", a, 440.0f, centsTolPure));
    }

    // 2. Inter-bin tone — IF correction should pull off-bin frequency to truth.
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        pushSamples (a, sine (runLength, sampleRate, 442.0), blockSize);
        run (expectFrequency ("pure 442 Hz (between bins)", a, 442.0f, centsTolPure));
    }

    // 3. High frequency.
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        pushSamples (a, sine (runLength, sampleRate, 5000.0), blockSize);
        run (expectFrequency ("pure 5000 Hz", a, 5000.0f, centsTolPure));
    }

    // 4. Low frequency (near bass — bin spacing matters more here).
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        pushSamples (a, sine (runLength, sampleRate, 110.0), blockSize);
        run (expectFrequency ("pure 110 Hz (A2)", a, 110.0f, centsTolPure));
    }

    // 5. Two-tone — both peaks must be detected and IF-correct.
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        pushSamples (a,
                     sineSum (runLength, sampleRate, 440.0f, 0.5f, 660.0f, 0.4f),
                     blockSize);
        auto peaks = a.getResolvedPeaks();
        auto* p1 = nearestPeak (peaks, 440.0f);
        auto* p2 = nearestPeak (peaks, 660.0f);
        const bool ok1 = (p1 != nullptr) && std::abs (centsBetween (p1->frequencyHz, 440.0f)) < centsTolPure;
        const bool ok2 = (p2 != nullptr) && std::abs (centsBetween (p2->frequencyHz, 660.0f)) < centsTolPure;
        const bool ok  = ok1 && ok2;
        std::printf ("  %-44s 440=%s  660=%s  %s\n",
                     "two-tone 440 + 660 Hz",
                     ok1 ? "ok" : "miss",
                     ok2 ? "ok" : "miss",
                     ok ? "PASS" : "*** FAIL");
        run ({ "two-tone", ok });
    }

    // 6. Silence — no peaks should be reported above the absolute floor.
    {
        autojust::PeakAnalyzer a (fftOrder, overlap);
        a.prepare (sampleRate, 1, blockSize);
        pushSamples (a, std::vector<float> ((size_t) runLength, 0.0f), blockSize);
        const auto peaks = a.getResolvedPeaks();
        const bool ok = peaks.empty();
        std::printf ("  %-44s reported %d peaks  %s\n",
                     "silence (expect 0 peaks)",
                     (int) peaks.size(),
                     ok ? "PASS" : "*** FAIL");
        run ({ "silence", ok });
    }

    std::printf ("\n%d / %d tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
