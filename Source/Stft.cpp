#include "Stft.h"

#include <cmath>
#include <algorithm>

namespace autojust
{

Stft::Stft (int fftOrderIn, int overlapFactor)
    : fftOrder (fftOrderIn),
      fftSize  (1 << fftOrderIn),
      hopSize  (fftSize / overlapFactor),
      fft      (fftOrderIn)
{
    // Hann² fails COLA for overlap=2 (residual cos(4πt/N) ripple); >=3 is constant.
    jassert (overlapFactor >= 3);
    jassert ((fftSize % overlapFactor) == 0);

    analysisWindow.resize  ((size_t) fftSize);
    synthesisWindow.resize ((size_t) fftSize);

    // Hann analysis + Hann synthesis. Their product Hann^2 summed across
    // frames at hop = N/overlap is constant = overlap/2 (for overlap >= 2).
    // Bake the inverse of that into the synthesis window so OLA reconstructs
    // unity gain.
    const float twoPi = juce::MathConstants<float>::twoPi;
    for (int n = 0; n < fftSize; ++n)
    {
        const float w = 0.5f * (1.0f - std::cos (twoPi * (float) n / (float) fftSize));
        analysisWindow[(size_t) n] = w;
        synthesisWindow[(size_t) n] = w;
    }

    // OLA normalization for Hann² (analysis Hann × synthesis Hann) at hop H = N/L:
    //   w²(n) = 3/8 - (1/2)cos(2πn/N) + (1/8)cos(4πn/N)
    // OLA at t sums L overlapping frames; the cos(2π/N) and cos(4π/N) phasor sums
    // vanish for L ≥ 3, leaving constant 3/8 · L. Divide synthesis window by that
    // for unity reconstruction gain. JUCE's performRealOnlyInverseTransform
    // already includes the 1/N normalization.
    const float olaSum = 0.375f * (float) overlapFactor;
    for (auto& s : synthesisWindow) s /= olaSum;
}

void Stft::prepare (int numChannels, int /*maxBlockSize*/)
{
    channels.assign ((size_t) numChannels, ChannelState{});
    for (auto& ch : channels)
    {
        ch.inputRing.assign  ((size_t) fftSize, 0.0f);
        ch.outputRing.assign ((size_t) fftSize, 0.0f);
        ch.fftWork.assign    ((size_t) (2 * fftSize), 0.0f);
        ch.writePos   = 0;
        ch.hopCounter = 0;
    }
}

void Stft::reset()
{
    for (auto& ch : channels)
    {
        std::fill (ch.inputRing.begin(),  ch.inputRing.end(),  0.0f);
        std::fill (ch.outputRing.begin(), ch.outputRing.end(), 0.0f);
        std::fill (ch.fftWork.begin(),    ch.fftWork.end(),    0.0f);
        ch.writePos   = 0;
        ch.hopCounter = 0;
    }
}

void Stft::runFrame (int channel)
{
    auto& ch = channels[(size_t) channel];

    // Copy windowed frame from inputRing[writePos..writePos+N) into fftWork
    // (inputRing is read in chronological order: oldest sample is at writePos).
    auto* work = ch.fftWork.data();
    for (int n = 0; n < fftSize; ++n)
    {
        const int src = (ch.writePos + n) % fftSize;
        work[n] = ch.inputRing[(size_t) src] * analysisWindow[(size_t) n];
    }
    // Zero the upper half (juce::dsp::FFT real-only forward expects 2N buffer).
    std::fill (work + fftSize, work + 2 * fftSize, 0.0f);

    fft.performRealOnlyForwardTransform (work);
    processSpectrum (work, fftSize);
    fft.performRealOnlyInverseTransform (work);

    // OLA-add windowed inverse into outputRing[writePos..writePos+N).
    for (int n = 0; n < fftSize; ++n)
    {
        const int dst = (ch.writePos + n) % fftSize;
        ch.outputRing[(size_t) dst] += work[n] * synthesisWindow[(size_t) n];
    }
}

void Stft::process (juce::AudioBuffer<float>& buffer)
{
    const int numChans   = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    for (int c = 0; c < numChans; ++c)
    {
        auto& ch = channels[(size_t) c];
        auto* data = buffer.getWritePointer (c);

        for (int i = 0; i < numSamples; ++i)
        {
            const float in = data[i];

            // Read output (OLA accumulator) at the current write position
            // BEFORE we overwrite it with the new input — this is the sample
            // that has finished accumulating across all overlapping frames.
            const float out = ch.outputRing[(size_t) ch.writePos];
            ch.outputRing[(size_t) ch.writePos] = 0.0f; // consume

            ch.inputRing[(size_t) ch.writePos] = in;

            ch.writePos = (ch.writePos + 1) % fftSize;
            ++ch.hopCounter;
            if (ch.hopCounter >= hopSize)
            {
                ch.hopCounter = 0;
                runFrame (c);
            }

            data[i] = out;
        }
    }
}

} // namespace autojust
