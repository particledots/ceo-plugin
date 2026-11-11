// The static art of the Ceo panel (option C "Sparks"), drawn ONCE into images and reused:
// knob body, knob scale ticks, layer dial body, display bezel + glass, display scan lines, panel ground
// (gradient, grain, bevel, screws, title) and the soft "halo" sprites used by the sparks.
//
// Images are made at the pixel scale the panel is really shown at (window size times screen scale), rounded to
// quarter steps, so they stay sharp when the window is resized or on a Retina screen, and are only redrawn when
// that scale changes. One CeoArt is shared by every open editor (juce::SharedResourcePointer).

#pragma once

#include "CeoStyle.h"

#include <array>
#include <map>

namespace CeoGeo
{
    // A knob is drawn in an 84 x 84 square around the same centre as before (the scale ticks reach beyond the old 60 x 60).
    constexpr int   knobBox     = 84;
    constexpr float knobCentre  = 42.0f;

    // The stream display: the glass keeps its place (560 x 126); the bezel is 6 units wide all round.
    constexpr int   displayMargin = 6;
    constexpr int   glassW = 560, glassH = 126;
    constexpr int   displayW = glassW + 2 * displayMargin, displayH = glassH + 2 * displayMargin;

    // The layer dial component (see LayerDial) and where its middle is.
    constexpr int   dialW = 280, dialH = 296;
    constexpr float dialCx = 140.0f, dialCy = 140.0f;

    // Angles of the seven layer sockets, degrees from 12 o'clock, clockwise.
    constexpr float socketAngle[7] = { -135.0f, -90.0f, -45.0f, 0.0f, 45.0f, 90.0f, 135.0f };
    constexpr float socketRadius = 92.0f;
    constexpr float letterRadius = 126.0f;
}

class CeoArt
{
public:
    CeoArt();
    ~CeoArt();

    // The art that is shared right now (null when no editor exists). Used by CeoStyle::font and friends.
    static CeoArt* current() noexcept;

    // Fonts
    juce::Font jost (float size) const;
    juce::Font jostMedium (float size) const;
    juce::Font jostItalic (float size) const;
    juce::Font title (float size) const;
    juce::Font greek (float size) const;

    // Pixel scale -> the scale the cached images are made at (quarter steps, 0.5 to 3).
    static float quantise (float physicalScale) noexcept;

    // Cached images. 'physicalScale' is what Graphics::getInternalContext().getPhysicalPixelScaleFactor() says.
    // Each image covers its logical size (given in CeoGeo) and is meant to be drawn stretched into that rectangle.
    const juce::Image& knobBody (float physicalScale);                // 84 x 84: shadow, knurled ring, brass cap
    const juce::Image& knobTicks (int litTicks, float physicalScale); // 84 x 84: 11 scale ticks, the first litTicks (1..11) lit and glowing
    const juce::Image& dialBody (float physicalScale);                // 280 x 296: shadow, ring, cap, seven sockets, "Layer"
    const juce::Image& displayBack (float physicalScale);             // 572 x 138: bezel and glass
    const juce::Image& displayOverlay (float physicalScale);          // 572 x 138: scan lines, sheen, inner shadow

    // Soft round glow sprites (64 x 64). 0..5 = layer colours, 6 = cream (Strike), 7 = gold (lamps, needle).
    const juce::Image& halo (int index);

    // The panel ground, drawn with 'g' already scaled to the 784 x 672 panel space. 'physicalScale' is the pixel scale of 'g'.
    void paintGround (juce::Graphics& g, float physicalScale);

private:
    struct Cached
    {
        float scale = 0.0f;
        juce::Image image;
    };

    const juce::Image& ensure (Cached&, float physicalScale, int logicalW, int logicalH,
                               const std::function<void (juce::Graphics&)>& draw);

    void paintKnobBody (juce::Graphics&);
    void paintKnobTicks (juce::Graphics&, int lit);
    void paintDialBody (juce::Graphics&);
    void paintDisplayBack (juce::Graphics&);
    void paintDisplayOverlay (juce::Graphics&);
    const juce::Image& noiseTile();

    juce::Typeface::Ptr jostRegular, jostMed, jostIt, titleFace, greekFace;

    Cached knobBodyCache, dialBodyCache, displayBackCache, displayOverlayCache;
    std::map<int, Cached> tickCache;
    std::array<juce::Image, 8> halos;
    juce::Image noise;
};
