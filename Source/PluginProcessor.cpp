#include "PluginProcessor.h"
#include "TonicEstimator.h"

namespace
{

/** PGM widget that binds a juce::Label's text to the AudioProcessor's
    tonic-label Value (which is updated on the message thread by a timer). */
class TonicLabelItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY (TonicLabelItem)

    TonicLabelItem (foleys::MagicGUIBuilder& b, juce::ValueTree node)
        : foleys::GuiItem (b, node)
    {
        addAndMakeVisible (label);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));

        if (auto* mps = dynamic_cast<foleys::MagicProcessorState*> (&b.getMagicState()))
            if (auto* proc = mps->getProcessor())
                if (auto* aj = dynamic_cast<AutoJustAudioProcessor*> (proc))
                    label.getTextValue().referTo (aj->getTonicLabelValue());
    }

    juce::Component* getWrappedComponent() override { return &label; }

    /** Called by PGM after parsing XML attrs; nothing to do — text is bound
        to the processor's juce::Value and updates via that path. */
    void update() override {}

private:
    juce::Label label;
};

} // anonymous namespace

AutoJustAudioProcessor::AutoJustAudioProcessor()
{
    bypassParam       = treeState.getRawParameterValue ("bypass");
    snapStrength      = treeState.getRawParameterValue ("snapStrength");
    testChordParam    = treeState.getRawParameterValue ("testChord");
    testChordVolParam = treeState.getRawParameterValue ("testChordVol");
    tonicLockParam    = treeState.getRawParameterValue ("tonicLock");
    tonicPitchParam   = treeState.getRawParameterValue ("tonicPitch");
    driftRateParam    = treeState.getRawParameterValue ("driftRate");
    bassBiasParam     = treeState.getRawParameterValue ("bassBias");

    tonicLabelValue.setValue (juce::String ("Tonic: —"));

    // Help-panel toggle: two MagicState properties drive `active=...` on the
    // `hide-main` / `hide-help` style classes (true ⇒ class is active ⇒ panel
    // collapses to zero size). Triggers wired to the Help / Back buttons flip
    // both at once so exactly one panel is showing.
    //
    // After toggling the properties we call `builder.updateComponents()` —
    // pgmf's local patch in foleys_GuiItem::updateInternal() short-circuits
    // after the first call (didUpdateInternal flag), so live class-`active`
    // changes don't relayout. updateComponents() rebuilds the GuiItem tree
    // fresh, which is the same thing the editor's File→Refresh menu does.
    magicState.getPropertyAsValue ("mainHidden").setValue (false);
    magicState.getPropertyAsValue ("helpHidden").setValue (true);
    // Deferred so the rebuild does not run while we're still inside the
    // TextButton's onClick callback — updateComponents() destroys the whole
    // GuiItem tree (the button included), which would be a use-after-free.
    auto refreshGui = [this]
    {
        juce::MessageManager::callAsync ([this]
        {
            if (auto* magicEditor = dynamic_cast<foleys::MagicPluginEditor*> (getActiveEditor()))
                magicEditor->getGUIBuilder().updateComponents();
        });
    };
    magicState.addTrigger ("showHelp", [this, refreshGui]
    {
        std::printf ("AutoJust: showHelp trigger fired\n"); std::fflush (stdout);
        magicState.getPropertyAsValue ("helpHidden").setValue (false);
        magicState.getPropertyAsValue ("mainHidden").setValue (true);
        refreshGui();
    });
    magicState.addTrigger ("hideHelp", [this, refreshGui]
    {
        std::printf ("AutoJust: hideHelp trigger fired\n"); std::fflush (stdout);
        magicState.getPropertyAsValue ("helpHidden").setValue (true);
        magicState.getPropertyAsValue ("mainHidden").setValue (false);
        refreshGui();
    });

    magicState.setGuiValueTree (BinaryData::AutoJust_xml, BinaryData::AutoJust_xmlSize);

    startTimerHz (25); // 40 ms — well under perceptual threshold for label updates
}

AutoJustAudioProcessor::~AutoJustAudioProcessor()
{
    stopTimer();
}

void AutoJustAudioProcessor::timerCallback()
{
    const float t = analyzer.getTonicEstimator().getTonicCents();

    // Convert cents-from-A4 to nearest semitone-class + signed cents offset.
    // Pitch class index: bin 0 = A, advancing in semitones.
    static const char* const names[12] = {
        "A", "Bb", "B", "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab"
    };
    int   semi  = (int) std::round (t / 100.0f);
    semi = ((semi % 12) + 12) % 12;
    const float resid = t - 100.0f * std::round (t / 100.0f);

    const auto s = juce::String::formatted ("Tonic: %s  %+.1fc  (raw %.1f)",
                                            names[semi], resid, t);
    tonicLabelValue.setValue (s);

    if (std::abs (t - lastReportedTonic) > 5.0f)
    {
        std::printf ("AutoJust tonic → %s\n", s.toRawUTF8());
        std::fflush (stdout);
        lastReportedTonic = t;
    }
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

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "testChord", 1 }, "Test Chord", false));

    // Test-chord output level (linear amplitude). 0 = silent, 1 = full level
    // (the per-voice amps in processBlock; sums to roughly 0.7 peak).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "testChordVol", 1 }, "Test Chord Vol",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "tonicLock", 1 }, "Tonic Lock", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "tonicPitch", 1 }, "Tonic Pitch",
        juce::StringArray { "A","Bb","B","C","Db","D","Eb","E","F","Gb","G","Ab" },
        3 /* default index = C */));

    // 5..200 cents/sec spans "rock-stable mastering" to "snappy live demo"
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "driftRate", 1 }, "Drift Rate (c/s)",
        juce::NormalisableRange<float> (5.0f, 200.0f, 0.1f), 60.0f));

    // 0..3: 0 = no bias (any pitch class can win), 1 = 1/f, 3 = strong bass
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bassBias", 1 }, "Bass Bias",
        juce::NormalisableRange<float> (0.0f, 3.0f, 0.01f), 1.0f));

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

    // Test chord: when on, replace input with a sustained ET C major triad so
    // the user can A/B the snap effect without sourcing audio. Crank Snap
    // Strength: the major 3rd's beating against the root should drop sharply
    // as the E pulls down to JI 5/4.
    const bool testChord = (testChordParam != nullptr) && (*testChordParam > 0.5f);
    if (testChord)
    {
        constexpr double freqs[3]    = { 261.6256, 329.6276, 391.9954 }; // C4, E4 (ET), G4 (ET)
        constexpr float  voiceAmps[3] = { 0.32f,    0.18f,    0.18f    }; // C dominant
        const float gain  = (testChordVolParam != nullptr) ? testChordVolParam->load() : 1.0f;
        const double sr = getSampleRate();
        const int    n  = buffer.getNumSamples();
        const double twoPi = juce::MathConstants<double>::twoPi;

        auto* w0 = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int v = 0; v < 3; ++v)
            {
                s += voiceAmps[v] * (float) std::sin (testPhase[v]);
                testPhase[v] += twoPi * freqs[v] / sr;
                if (testPhase[v] > twoPi) testPhase[v] -= twoPi;
            }
            w0[i] = gain * s;
        }
        for (int ch = 1; ch < totalOut; ++ch)
            std::copy (w0, w0 + n, buffer.getWritePointer (ch));
    }

    // v1b: bypass=false enables JI snapping; snapStrength scales attractor pull.
    const bool bypass = (bypassParam != nullptr) && (*bypassParam > 0.5f);
    analyzer.setEnabled (! bypass);
    if (snapStrength != nullptr)
        analyzer.setSnapStrength (*snapStrength);

    // Tier-1 fine-tune params → tonic estimator.
    auto& tonic = analyzer.getTonicEstimator();
    if (driftRateParam != nullptr) tonic.setMaxDriftCpsPerSec (*driftRateParam);
    if (bassBiasParam  != nullptr) tonic.setBassBiasExponent  (*bassBiasParam);

    const bool tonicLocked = (tonicLockParam != nullptr) && (*tonicLockParam > 0.5f);
    if (tonicLocked && tonicPitchParam != nullptr)
    {
        // tonicPitch index 0 = A, 1 = Bb, ..., 11 = Ab; cents from A in 100c steps.
        const int pitchIdx = (int) (*tonicPitchParam + 0.5f);
        tonic.setLock (true, 100.0f * (float) pitchIdx);
    }
    else
    {
        tonic.setLock (false);
    }

    analyzer.process (buffer);
}

void AutoJustAudioProcessor::initialiseBuilder (foleys::MagicGUIBuilder& builder)
{
    builder.registerJUCEFactories();
    builder.registerJUCELookAndFeels();
    builder.registerFactory ("TonicLabel", &TonicLabelItem::factory);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoJustAudioProcessor();
}
