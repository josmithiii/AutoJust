// Streaming STFT scaffolding for AutoJust.
// Default behavior is identity: analyze → (no modification) → synthesize, with
// reconstruction error at machine epsilon. Subsequent steps override
// processSpectrum() to do peak picking, retuning, etc.
//
// Layout: Hann analysis + Hann synthesis windows, 75% overlap by default
// (overlap=4 → hopSize=fftSize/4). Hann*Hann at hop=N/4 sums to 1.5 across
// frames (Constant Overlap-Add); normalization absorbed into synthesisWindow.
//
// Spectrum format follows juce::dsp::FFT::performRealOnlyForwardTransform:
// `data` is a 2*fftSize float buffer holding packed complex bins. Bin k for
// k in [0, fftSize/2] is at (data[2k], data[2k+1]); the Nyquist bin is real
// (imag part stored at data[1] per JUCE convention — we pass through it
// unchanged when not modifying the spectrum).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace autojust
{

class Stft
{
public:
    Stft (int fftOrderIn = 12, int overlapFactor = 4);
    virtual ~Stft() = default;

    void prepare (int numChannels, int maxBlockSize);
    void reset();

    /** In-place STFT round-trip. Output is delayed by getLatencySamples(). */
    void process (juce::AudioBuffer<float>& buffer);

    int getLatencySamples() const noexcept { return fftSize; }
    int getFftSize()        const noexcept { return fftSize; }
    int getHopSize()        const noexcept { return hopSize; }

protected:
    /** Override to modify the spectrum in-place before resynthesis.
        Default implementation is identity. */
    virtual void processSpectrum (float* /*data*/, int /*fftSize*/) {}

private:
    void runFrame (int channel);

    int fftOrder = 0;
    int fftSize  = 0;
    int hopSize  = 0;

    juce::dsp::FFT fft;

    std::vector<float> analysisWindow;
    std::vector<float> synthesisWindow;

    struct ChannelState
    {
        std::vector<float> inputRing;   // size = fftSize
        std::vector<float> outputRing;  // size = fftSize, OLA accumulator
        std::vector<float> fftWork;     // size = 2 * fftSize
        int writePos = 0;               // next write index in inputRing / outputRing
        int hopCounter = 0;             // increments per sample, fires a frame at hopSize
    };
    std::vector<ChannelState> channels;
};

} // namespace autojust
