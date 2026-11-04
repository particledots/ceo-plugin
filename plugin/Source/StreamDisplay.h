// The stream display (see the approved mockup, ceo-panel-mockup.html):
// a glowing ribbon whose thickness follows the loudness of the selected layer, scattered grain
// specks, a dashed play line above the ribbon and a vertical Scan marker.
// For Omega the ribbon shades through all six layer colours, left to right.
//
// The specks are real: each one is a grain the engine has just started, at the place in the
// layer where it started. The panel's timer calls tick() about 30 times a second.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <ceo/Engine.h>
#include <vector>

class StreamDisplay : public juce::Component
{
public:
    StreamDisplay (const ceo::Engine& engineToShow, juce::AudioProcessorValueTreeState& state);

    // Reads the engine (new grain starts, recording changes) and repaints.
    void tick();

    void paint (juce::Graphics&) override;

    // 0..1, how recently a forward / backward grain started (for the two direction lights).
    float forwardActivity() const { return forwardLevel; }
    float reverseActivity() const { return reverseLevel; }

private:
    struct Speck
    {
        float  position   = 0.0f;   // 0..1 along the display
        float  vertical   = 0.0f;   // 0..1, random: how far from the ribbon
        float  sideways   = 0.0f;   // -0.5..0.5, random
        float  size       = 1.0f;
        float  brightness = 1.0f;
        bool   below      = false;
        bool   strike     = false;
        double born       = 0.0;    // seconds
    };

    static constexpr int kRibbonPoints = 72;      // six layers of 12 in Omega
    static constexpr double kSpeckLife = 1.6;     // seconds

    void rebuildRibbon (int layer);
    float ribbonHalfHeight (int index, float scale) const;
    juce::Colour colourAt (float position, int layer) const;

    const ceo::Engine& engine;
    std::atomic<float>* layerValue = nullptr;
    std::atomic<float>* scanValue  = nullptr;
    std::atomic<float>* sprayValue = nullptr;
    std::atomic<float>* followValue = nullptr;

    float ribbon[kRibbonPoints] = {};
    int   shownLayer = -1;
    uint32_t shownRevision = 0;
    int   ticksSinceRibbon = 0;

    std::vector<Speck> specks;
    uint32_t grainCursor = 0;
    juce::Random random;

    float forwardLevel = 0.0f;
    float reverseLevel = 0.0f;
    bool  recording = false;
};
