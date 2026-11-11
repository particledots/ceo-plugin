// The stream display, option C "Sparks" (milestone 13; the older looks are in the mockups):
// a phosphor-style ribbon on near-black glass whose thickness follows the loudness of the selected layer,
// with scan lines and curved-glass sheen on top, a dashed play line above the ribbon and an amber Scan needle.
// For Omega the ribbon shades through all six layer colours, left to right.
//
// The sparks are real: every grain the engine starts (Engine::takeGrainStarts) is thrown off the ribbon at the
// place in the layer where it started, as a comet-tailed spark that flashes white-hot, cools to the layer colour
// and fades. How long it lives follows the Length knob (the real grain length), how far it flies follows Spray.
// Strike grains are bigger and cream.
//
// Cost control: the panel's timer calls tick() 30 times a second; at most kMaxSparks sparks are alive at once
// (fixed array, nothing allocated while playing); the bezel, glass, scan lines and the whole ribbon (with its
// glow) are cached images, the ribbon only redrawn when the envelope or the layer changes; the glows of the
// sparks are small pre-drawn sprites. When nothing moves, nothing is repainted.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <ceo/Engine.h>
#include "CeoArt.h"

#include <array>

class StreamDisplay : public juce::Component
{
public:
    StreamDisplay (const ceo::Engine& engineToShow, juce::AudioProcessorValueTreeState& state);

    // Reads the engine (new grain starts, recording changes) and repaints what changed.
    void tick();

    void paint (juce::Graphics&) override;

    // 0..1, how recently a forward / backward grain started (for the two direction lights).
    float forwardActivity() const { return forwardLevel; }
    float reverseActivity() const { return reverseLevel; }

    // With reduced motion the sparks do not fly: each stays where its grain started and fades.
    void setReducedMotion (bool shouldReduce) { reducedMotion = shouldReduce; }

    static constexpr int kMaxSparks = 96;
    int liveSparks() const noexcept { return sparkCount; }

    // For the bench and the local check: how many times the ribbon image had to be redrawn.
    int ribbonRenders() const noexcept { return ribbonRenderCount; }

private:
    struct Spark
    {
        float  x = 0.0f, y = 0.0f;        // where the grain started (display coordinates)
        float  dx = 0.0f, dy = 0.0f;      // how far it flies
        float  size = 1.0f;
        float  brightness = 1.0f;
        float  life = 1.0f;               // seconds
        double born = 0.0;                // seconds
        int    colour = 0;                // 0..5 layer colour index
        bool   strike = false;
    };

    struct Geo
    {
        juce::Rectangle<float> glass;
        float x0 = 0.0f, width = 0.0f, midY = 0.0f, scale = 1.0f;
    };

    static constexpr int kRibbonPoints = 72;      // six layers of 12 in Omega

    Geo geometry() const;
    void rebuildRibbon (int layer);
    void renderRibbon (float scale);
    float ribbonHalfHeight (int index, float scale) const;
    int colourIndexAt (float position, int layer) const;
    float markerPosition (int layer) const;
    void addSpark (const ceo::GrainStart&, int layer, float spray, float grainSeconds, double now);
    void drawSparks (juce::Graphics&, double now);

    const ceo::Engine& engine;
    std::atomic<float>* layerValue  = nullptr;
    std::atomic<float>* scanValue   = nullptr;
    std::atomic<float>* sprayValue  = nullptr;
    std::atomic<float>* lengthValue = nullptr;
    std::atomic<float>* followValue = nullptr;

    float ribbon[kRibbonPoints] = {};
    int   shownLayer = -1;
    uint32_t shownRevision = 0;
    int   ticksSinceRibbon = 0;

    juce::Image ribbonImage;
    float ribbonScale = 0.0f;
    bool  ribbonDirty = true;
    int   ribbonRenderCount = 0;

    juce::Path tailPath;                          // reused for every spark's tail
    std::array<Spark, kMaxSparks> sparks;
    int sparkCount = 0;
    uint32_t grainCursor = 0;
    juce::Random random;
    bool reducedMotion = false;

    float forwardLevel = 0.0f;
    float reverseLevel = 0.0f;
    bool  recording = false;
    float lastMarker = -1.0f;

    juce::SharedResourcePointer<CeoArt> art;
};
