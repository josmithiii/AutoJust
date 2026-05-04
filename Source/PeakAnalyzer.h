// Peak picker + instantaneous-frequency estimator built on top of Stft.
// v0.3: detect spectral peaks, compute IF-corrected frequency via consecutive-
// frame phase difference, expose the latest peak set as a thread-safe snapshot.
// No retuning yet — pure analysis / diagnostics.
//
// IF estimation (phase-vocoder method):
//   For peak at bin k in frame at hop H:
//     dphi          = phase[k] - prev_phase[k]
//     expected      = 2π · k · H / N           (steady-rotation contribution)
//     princ_dphi    = wrap_to_pi(dphi - expected)
//     true_bin      = k + princ_dphi · N / (2π · H)
//     true_freq_hz  = true_bin · sampleRate / N
//
// The IF correction is what gives sub-bin frequency resolution that JI
// retuning needs (FFT bin spacing at SR=44100, N=4096 is ≈10.8 Hz, which
// is ~40 cents at A4; the IF correction routinely pulls this below 1 cent
// for stable harmonic content).

#pragma once

#include "Stft.h"

#include <vector>
#include <atomic>
#include <mutex>

namespace autojust
{

struct ResolvedPeak
{
    float frequencyHz = 0.0f;   ///< IF-corrected frequency
    float magnitude   = 0.0f;   ///< |X[k]|
    int   bin         = 0;      ///< nearest FFT bin
};

class PeakAnalyzer : public Stft
{
public:
    PeakAnalyzer (int fftOrder = 12, int overlapFactor = 4);

    void prepare (double sampleRate, int numChannels, int maxBlockSize);
    void reset();

    /** Snapshot of the most recent resolved peaks. Thread-safe; cheap to
        call from the GUI / message thread. */
    std::vector<ResolvedPeak> getResolvedPeaks() const;

    /** Peak qualifies if magnitude > median(magnitudes) · 10^(thresholdDb/20). */
    void setPeakThresholdDb (float db) noexcept       { peakThresholdDb = db; }
    /** Hard floor — peaks with magnitude below this are ignored (default 1e-4). */
    void setAbsoluteFloor   (float v)  noexcept       { absoluteFloor = v; }
    /** Cap on the number of peaks reported per frame. */
    void setMaxPeaks        (int n)    noexcept       { maxPeaks = n; }

    /** For tests: which channel's peaks are exposed via getResolvedPeaks(). */
    void setReportChannel   (int c)    noexcept       { reportChannel = c; }

protected:
    void processSpectrum (float* data, int fftSize, int channel) override;

private:
    double sampleRate     = 44100.0;
    float  peakThresholdDb = 12.0f;
    float  absoluteFloor   = 1.0e-4f;
    int    maxPeaks        = 32;
    int    reportChannel   = 0;

    int numBins = 0;  // fftSize/2 + 1

    // Per-channel previous-frame phase, indexed [channel][bin].
    std::vector<std::vector<float>> prevPhase;

    // Latest snapshot, guarded by mutex (write from audio thread, read from GUI).
    // Mutex is uncontended in steady state and the audio thread only does
    // try_lock; if a reader holds it we drop this frame's update.
    mutable std::mutex peaksMutex;
    std::vector<ResolvedPeak> latestPeaks;
};

} // namespace autojust
