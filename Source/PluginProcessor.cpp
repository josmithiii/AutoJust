// AutoJust — v0 scaffolding: identity passthrough.
// The DSP pipeline (STFT → peak picking → harmonic grouping → JI grid →
// peak-locked spectral translation → ISTFT) lives behind processBlock and
// will be filled in incrementally per AutoJust_PLAN.md.

#include "PluginProcessor.h"

AutoJustAudioProcessor::AutoJustAudioProcessor()
{
    bypassParam  = treeState.getRawParameterValue ("bypass");
    snapStrength = treeState.getRawParameterValue ("snapStrength");

    magicState.setGuiValueTree (BinaryData::AutoJust_xml, BinaryData::AutoJust_xmlSize);
}

juce::AudioProcessorValueTreeState::ParameterLayout
AutoJustAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "snapStrength", 1 }, "Snap Strength",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    return layout;
}

void AutoJustAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analyzer.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()), samplesPerBlock);
    setLatencySamples (analyzer.getLatencySamples());
}

void AutoJustAudioProcessor::releaseResources()
{
    analyzer.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AutoJustAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return mainOut == layouts.getMainInputChannelSet();
}
#endif

void AutoJustAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto totalIn  = getTotalNumInputChannels();
    const auto totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // v1b: bypass=false enables JI snapping; snapStrength scales attractor pull.
    const bool bypass = (bypassParam != nullptr) && (*bypassParam > 0.5f);
    analyzer.setEnabled (! bypass);
    if (snapStrength != nullptr)
        analyzer.setSnapStrength (*snapStrength);

    analyzer.process (buffer);
}

void AutoJustAudioProcessor::initialiseBuilder (foleys::MagicGUIBuilder& builder)
{
    builder.registerJUCEFactories();
    builder.registerJUCELookAndFeels();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoJustAudioProcessor();
}
