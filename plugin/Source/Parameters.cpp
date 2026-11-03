#include "Parameters.h"

namespace CeoParams
{

static std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const char* name, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 },
                                                        name,
                                                        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f),
                                                        defaultValue);
}

static std::unique_ptr<juce::AudioParameterBool> button (const char* id, const char* name)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, false);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Top row
    layout.add (knob (inputLevel,  "Input level",  0.5f));
    layout.add (knob (dryWet,      "Dry/wet",      0.5f));
    layout.add (knob (outputLevel, "Output level", 0.5f));

    // Capture button, layer dial, Strike button
    layout.add (button (capture, "Capture"));

    // Seven positions: alpha, beta, gamma, delta, epsilon, zeta, then Omega (all six layers).
    juce::StringArray layerNames;
    layerNames.add (juce::String::fromUTF8 ("\xce\xb1"));   // alpha
    layerNames.add (juce::String::fromUTF8 ("\xce\xb2"));   // beta
    layerNames.add (juce::String::fromUTF8 ("\xce\xb3"));   // gamma
    layerNames.add (juce::String::fromUTF8 ("\xce\xb4"));   // delta
    layerNames.add (juce::String::fromUTF8 ("\xce\xb5"));   // epsilon
    layerNames.add (juce::String::fromUTF8 ("\xce\xb6"));   // zeta
    layerNames.add (juce::String::fromUTF8 ("\xce\xa9"));   // Omega
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { layer, 1 },
                                                              "Layer", layerNames, 0));

    layout.add (button (strike, "Strike"));

    // Grain knobs, in panel order. Centre-detent knobs start at 0.5.
    layout.add (knob (intensity,      "Intensity",       0.5f));
    layout.add (knob (grainWindow,    "Grain window",    0.5f));
    layout.add (knob (grainDirection, "Grain direction", 0.5f));
    layout.add (knob (length,         "Length",          0.5f));
    layout.add (knob (scan,           "Scan",            0.0f));
    layout.add (knob (spray,          "Spray",           0.0f));
    layout.add (knob (pitch,          "Pitch",           0.5f));
    layout.add (knob (pitchDeviation, "Pitch deviation", 0.5f));
    layout.add (knob (modAmount,      "Mod amount",      0.0f));
    layout.add (knob (hold,           "Hold",            0.5f));
    layout.add (knob (sense,          "Sense",           0.5f));
    layout.add (knob (dub,            "Dub",             0.5f));

    return layout;
}

} // namespace CeoParams
