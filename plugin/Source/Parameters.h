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
    inline constexpr const char* erase          = "erase";          // plain button for now (every click erases the selected layer)
    inline constexpr const char* undo           = "undo";           // plain button for now (every click undoes the last record-over)
    inline constexpr const char* follow         = "follow";         // Follow mode on/off (Scan becomes a speed control)
    inline constexpr const char* inputMode      = "inputMode";      // dropdown: Mono / Stereo
    inline constexpr const char* onsetCapture   = "onsetCapture";   // plain button: a detected attack starts recording (Mono input only)
    inline constexpr const char* modTarget      = "modTarget";      // dropdown: what the Mod amount knob controls

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

    // Milestone 8: two menus, added after the first release. New ids only; the ones above never change.
    inline constexpr const char* pitchSnap      = "pitchSnap";      // dropdown under Pitch: Off / Steps / Semitones
    inline constexpr const char* pitchScale     = "pitchScale";     // dropdown under Pitch deviation: Off + 28 scales

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
