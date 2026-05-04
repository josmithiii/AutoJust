// v1b end-to-end test: feed a detuned-from-JI dyad through the Retuner with
// snapStrength=1 and verify the upper note is pulled toward the JI ratio.
//
// Setup: a sustained C5 (root, dominant in the histogram) plus an ET-tuned
// E5 a major-3rd above it. ET major 3rd = 400 cents above C; JI 5/4 = 386.31
// cents above C. So with C as the tonic, E should be pulled DOWN by ~13.7
// cents.
//
// Verify by analyzing the output spectrum and checking the upper peak is
// closer to JI E than to ET E.

#include "../Source/Retuner.h"
#include "../Source/PeakAnalyzer.h"
#include "../Source/TonicEstimator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace
{

constexpr int    fftOrder   = 12;
constexpr int    overlap    = 4;
constexpr double sampleRate = 44100.0;
constexpr int    blockSize  = 256;

std::vector<float> dyad (int n, double f1, float a1, double f2, float a2)
{
    std::vector<float> x ((size_t) n);
    const double k1 = 2.0 * M_PI * f1 / sampleRate;
    const double k2 = 2.0 * M_PI * f2 / sampleRate;
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = a1 * (float) std::sin (k1 * (double) i)
                      + a2 * (float) std::sin (k2 * (double) i);
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

float centsBetween (float measured, float reference)
{
    return 1200.0f * std::log2 (measured / reference);
}

float detectNearestHz (const std::vector<float>& signal, double sr,
                       int latencyToSkip, float targetHz)
{
    autojust::PeakAnalyzer a (fftOrder, overlap);
    a.prepare (sr, 1, blockSize);
    std::vector<float> trimmed (signal.begin() + std::min ((int) signal.size(), latencyToSkip),
                                signal.end());
    analyze (a, trimmed);
    auto peaks = a.getResolvedPeaks();
    if (peaks.empty()) return -1.0f;

    const autojust::ResolvedPeak* best = nullptr;
    float bestErr = 1.0e30f;
    for (const auto& p : peaks)
    {
        const float err = std::abs (p.frequencyHz - targetHz);
        if (err < bestErr) { bestErr = err; best = &p; }
    }
    return best ? best->frequencyHz : -1.0f;
}

} // namespace

int main()
{
    int passed = 0, total = 0;

    std::printf ("Retuner JI test (5-limit, all-peaks soft attractor)\n");

    // C5 = 523.2511 Hz, ET-E5 = 659.2551 Hz, JI-E5 = 523.2511 * 5/4 = 654.0639
    // (so ET E is sharp of JI E by ~13.69 cents).
    constexpr double f_C  = 523.2511;
    constexpr double f_E_ET = 659.2551;
    const double f_E_JI = f_C * 5.0 / 4.0;          // 654.0639
    constexpr int N = (int) (sampleRate * 2.0);     // 2 seconds (settle tonic)

    // 1. Pass C alone first to bias the tonic estimator toward C, then add E.
    //    Both notes equal amplitude is fine once tonic is locked on C; for
    //    extra safety we make C louder so the histogram clearly picks C.
    auto in = dyad (N, f_C, 0.6f, f_E_ET, 0.4f);

    autojust::Retuner r (fftOrder, overlap);
    r.prepare (sampleRate, 1, blockSize);
    r.setEnabled (true);
    r.setSnapStrength (1.0f);
    // Speed up tonic settling for the test.
    r.getTonicEstimator().setHistogramTauSec   (0.3f);
    r.getTonicEstimator().setMaxDriftCpsPerSec (2400.0f);

    auto out = processThrough (r, in);
    const int latency = r.getLatencySamples();

    // Detect the upper peak in the output (around f_E_JI / f_E_ET).
    const float detectedE = detectNearestHz (out, sampleRate, latency,
                                             (float) ((f_E_ET + f_E_JI) * 0.5));
    // Detect the lower peak (should be unchanged: it's the tonic).
    const float detectedC = detectNearestHz (out, sampleRate, latency, (float) f_C);

    const float errE_vs_JI = centsBetween (detectedE, (float) f_E_JI);
    const float errE_vs_ET = centsBetween (detectedE, (float) f_E_ET);
    const float errC       = centsBetween (detectedC, (float) f_C);

    std::printf ("  tonic settled at %.2f cents\n",
                 r.getTonicEstimator().getTonicCents());
    std::printf ("  C peak:  detected=%.4f Hz   target=%.4f Hz   err=%+.3f cents\n",
                 detectedC, f_C, errC);
    std::printf ("  E peak:  detected=%.4f Hz\n", detectedE);
    std::printf ("           vs ET E (%.4f): %+.3f cents\n", f_E_ET, errE_vs_ET);
    std::printf ("           vs JI E (%.4f): %+.3f cents\n", f_E_JI, errE_vs_JI);

    // Pass criteria:
    // (1) C peak essentially unchanged (no shift expected for tonic).
    const bool okC = std::abs (errC) < 2.0f;
    // (2) E peak is closer to JI than to ET, and within ~3 cents of JI.
    const bool okE = std::abs (errE_vs_JI) < 3.0f &&
                     std::abs (errE_vs_JI) < std::abs (errE_vs_ET);

    std::printf ("\n  C unchanged:  %s\n",  okC ? "PASS" : "*** FAIL");
    std::printf ("  E snapped to JI: %s\n", okE ? "PASS" : "*** FAIL");
    if (okC) ++passed; ++total;
    if (okE) ++passed; ++total;

    // 2. snapStrength=0 should leave the spectrum identical (within JI mode).
    {
        autojust::Retuner r2 (fftOrder, overlap);
        r2.prepare (sampleRate, 1, blockSize);
        r2.setEnabled (true);
        r2.setSnapStrength (0.0f);
        r2.getTonicEstimator().setHistogramTauSec   (0.3f);
        r2.getTonicEstimator().setMaxDriftCpsPerSec (2400.0f);

        auto in2  = dyad (N, f_C, 0.6f, f_E_ET, 0.4f);
        auto out2 = processThrough (r2, in2);
        const float dE = detectNearestHz (out2, sampleRate, r2.getLatencySamples(), (float) f_E_ET);
        const float err = centsBetween (dE, (float) f_E_ET);
        const bool  ok  = std::abs (err) < 1.0f;
        std::printf ("\n  snapStrength=0: E stays at ET (err=%+.3f cents)  %s\n",
                     err, ok ? "PASS" : "*** FAIL");
        if (ok) ++passed; ++total;
    }

    std::printf ("\n%d / %d tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
