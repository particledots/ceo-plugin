// The list of controls Logic can see and automate.
// Names and order follow ceo-design-spec.md. Knobs run 0.0 to 1.0 for now; the
// engine decides later what each position means (seconds, percent, pitch...).
//
// The short ids below are what Logic stores in saved projects and automation,
// so they should not be renamed once people start using the plugin.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace CeoParams
{
    // Top row
    inline constexpr const char* inputLevel     = "inputLevel";
    inline constexpr const char* dryWet         = "dryWet";
    inline constexpr const char* outputLevel    = "outputLevel";

    // Buttons and layer dial
    inline constexpr const char* capture        = "capture";
    inline constexpr const char* layer          = "layer";
    inline constexpr const char* strike         = "strike";

    // Grain knobs
    inline constexpr const char* intensity      = "intensity";
    inline constexpr const char* grainWindow    = "grainWindow";
    inline constexpr const char* grainDirection = "grainDirection";
    inline constexpr const char* length         = "length";
    inline constexpr const char* scan           = "scan";
    inline constexpr const char* spray          = "spray";
    inline constexpr const char* pitch          = "pitch";
    inline constexpr const char* pitchDeviation = "pitchDeviation";
    inline constexpr const char* modAmount      = "modAmount";
    inline constexpr const char* hold           = "hold";
    inline constexpr const char* sense          = "sense";
    inline constexpr const char* dub            = "dub";

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
