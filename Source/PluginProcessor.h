// AutoJust — master-bus adaptive Just Intonation retuner.
// v0 scaffolding: identity passthrough with two stub parameters.
// See ../AutoJust_PLAN.md for the full design.

#pragma once

#include <JuceHeader.h>
#include "PeakAnalyzer.h"

class AutoJustAudioProcessor : public foleys::MagicProcessor
{
public:
    AutoJustAudioProcessor();
    ~AutoJustAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    void initialiseBuilder (foleys::MagicGUIBuilder& builder) override;

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState treeState
        { *this, nullptr, "PARAMETERS", createParameterLayout() };

    std::atomic<float>* bypassParam   { nullptr };
    std::atomic<float>* snapStrength  { nullptr };

    autojust::PeakAnalyzer analyzer { 12, 4 }; // 4096-sample frame, 75% overlap

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoJustAudioProcessor)
};
