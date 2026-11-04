#include "StreamDisplay.h"
#include "CeoLookAndFeel.h"
#include "Parameters.h"

#include <algorithm>
#include <atomic>
#include <cmath>

StreamDisplay::StreamDisplay (const ceo::Engine& engineToShow, juce::AudioProcessorValueTreeState& state)
    : engine (engineToShow)
{
    layerValue  = state.getRawParameterValue (CeoParams::layer);
    scanValue   = state.getRawParameterValue (CeoParams::scan);
    sprayValue  = state.getRawParameterValue (CeoParams::spray);
    followValue = state.getRawParameterValue (CeoParams::follow);

    specks.reserve (256);
    setInterceptsMouseClicks (false, false);
}

void StreamDisplay::rebuildRibbon (int layer)
{
    bool ok = true;
    if (layer < 6)
    {
        ok = engine.layerEnvelope (layer, ribbon, kRibbonPoints);
    }
    else
    {
        const int perLayer = kRibbonPoints / 6;
        for (int l = 0; l < 6 && ok; ++l)
            ok = engine.layerEnvelope (l, ribbon + l * perLayer, perLayer);
    }

    if (ok)
    {
        shownLayer = layer;
        shownRevision = engine.recordingRevision();
        ticksSinceRibbon = 0;
    }
}

void StreamDisplay::tick()
{
    const int layer = juce::jlimit (0, 6, juce::roundToInt (layerValue->load()));

    // Rebuild the ribbon when the layer dial moved or the recording changed (at most every other tick),
    // and now and then anyway.
    ++ticksSinceRibbon;
    if (layer != shownLayer
        || (engine.recordingRevision() != shownRevision && ticksSinceRibbon >= 2)
        || ticksSinceRibbon >= 30)
        rebuildRibbon (layer);

    // New grains become specks.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    ceo::GrainStart starts[64];
    const int count = engine.takeGrainStarts (grainCursor, starts, 64);
    for (int i = 0; i < count; ++i)
    {
        Speck s;
        s.position   = starts[i].position;
        s.vertical   = random.nextFloat();
        s.sideways   = random.nextFloat() - 0.5f;
        s.size       = 0.8f + random.nextFloat() * 1.2f;
        s.brightness = 0.5f + random.nextFloat() * 0.5f;
        s.below      = random.nextFloat() < 0.22f;
        s.strike     = starts[i].strike;
        s.born       = now;
        specks.push_back (s);

        if (starts[i].reversed)
            reverseLevel = 1.0f;
        else
            forwardLevel = 1.0f;
    }

    // Old specks go; never keep more than 240.
    specks.erase (std::remove_if (specks.begin(), specks.end(), [now] (const Speck& s) { return now - s.born > kSpeckLife; }),
                  specks.end());
    if (specks.size() > 240)
        specks.erase (specks.begin(), specks.begin() + (std::ptrdiff_t) (specks.size() - 240));

    forwardLevel *= 0.72f;
    reverseLevel *= 0.72f;
    recording = engine.isRecording();

    repaint();
}

float StreamDisplay::ribbonHalfHeight (int index, float scale) const
{
    // Loudness (RMS) to thickness: a full-scale-ish sound fills the display, quiet sound still shows,
    // an empty layer is a thin line.
    const float level = juce::jlimit (0.0f, 1.0f, ribbon[index] * 3.0f);
    return scale * (3.0f + 25.0f * std::pow (level, 0.6f));
}

juce::Colour StreamDisplay::colourAt (float position, int layer) const
{
    if (layer < 6)
        return CeoStyle::layerColour (layer);

    return CeoStyle::layerColour (juce::jlimit (0, 5, (int) (position * 6.0f)));
}

void StreamDisplay::paint (juce::Graphics& g)
{
    using namespace CeoStyle;

    const auto bounds = getLocalBounds().toFloat();
    const float pad    = 12.0f;
    const float x0     = pad;
    const float width  = bounds.getWidth() - 2.0f * pad;
    const float midY   = bounds.getHeight() * 0.548f;
    const float scale  = bounds.getHeight() / 84.0f;       // the mockup display is 84 high
    const int   layer  = juce::jlimit (0, 6, juce::roundToInt (layerValue->load()));

    g.setColour (displayBackground());
    g.fillRoundedRectangle (bounds, 6.0f);

    // Ribbon outline: top edge = middle line minus the thickness, bottom edge = a little under the middle line.
    auto xAt = [&] (int i) { return x0 + width * (float) i / (float) (kRibbonPoints - 1); };

    juce::Path ribbonPath, edgePath, headPath;
    for (int i = 0; i < kRibbonPoints; ++i)
    {
        const float h = ribbonHalfHeight (i, scale);
        if (i == 0)
        {
            ribbonPath.startNewSubPath (xAt (i), midY - h);
            headPath.startNewSubPath   (xAt (i), midY - h - 6.0f * scale);
        }
        else
        {
            ribbonPath.lineTo (xAt (i), midY - h);
            headPath.lineTo   (xAt (i), midY - h - 6.0f * scale);
        }
    }
    for (int i = kRibbonPoints - 1; i >= 0; --i)
    {
        const float low = midY + ribbonHalfHeight (i, scale) * 0.55f;
        ribbonPath.lineTo (xAt (i), low);
        if (i == kRibbonPoints - 1)
            edgePath.startNewSubPath (xAt (i), low);
        else
            edgePath.lineTo (xAt (i), low);
    }
    ribbonPath.closeSubPath();

    // Fill: one colour, or for Omega all six layer colours side by side.
    if (layer < 6)
    {
        g.setColour (layerColour (layer));
    }
    else
    {
        juce::ColourGradient gradient (layerColour (0), x0, 0.0f, layerColour (5), x0 + width, 0.0f, false);
        for (int s = 1; s < 5; ++s)
            gradient.addColour (((double) s + 0.5) / 6.0, layerColour (s));
        g.setGradientFill (gradient);
    }
    g.fillPath (ribbonPath);

    // Brighter along the top edge, fading downward (like the mask in the mockup).
    g.saveState();
    g.reduceClipRegion (ribbonPath, juce::AffineTransform());
    g.setGradientFill (juce::ColourGradient (displayBackground().withAlpha (0.0f), 0.0f, 0.0f,
                                             displayBackground().withAlpha (0.77f), 0.0f, bounds.getHeight(), false));
    g.fillRect (bounds);
    g.restoreState();

    // Thin cream edge line along the bottom of the ribbon.
    g.setColour (cream().withAlpha (0.8f));
    g.strokePath (edgePath, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

    // Dashed cream play line above the ribbon.
    {
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, headPath, dashes, 2);
        g.setColour (cream().withAlpha (0.85f));
        g.fillPath (dashed);
    }

    // Grain specks. They sit around the ribbon; the more Spray, the further they scatter.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float spread = scale * (4.0f + sprayValue->load() * 34.0f);
    for (const auto& s : specks)
    {
        const float fade = (float) juce::jlimit (0.0, 1.0, 1.0 - (now - s.born) / kSpeckLife);
        const int idx = juce::jlimit (0, kRibbonPoints - 1, (int) std::lround (s.position * (float) (kRibbonPoints - 1)));
        const float half = ribbonHalfHeight (idx, scale);

        float x = x0 + s.position * width + s.sideways * spread * 0.8f;
        float y = s.below ? midY + half * 0.55f + s.vertical * s.vertical * spread * 0.6f
                          : midY - half - s.vertical * s.vertical * spread;
        x = juce::jlimit (x0 - 4.0f, x0 + width + 4.0f, x);
        y = juce::jlimit (4.0f, bounds.getHeight() - 4.0f, y);

        const float r = s.size * (s.strike ? 1.8f : 1.0f);
        g.setColour ((s.strike ? cream() : colourAt (s.position, layer)).withAlpha (s.brightness * fade));
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
    }

    // Vertical cream marker: the Scan position, or in Follow mode the playhead.
    float markerPos = juce::jlimit (0.0f, 1.0f, scanValue->load());
    if (followValue->load() > 0.5f)
        markerPos = juce::jlimit (0.0f, 1.0f, (float) (engine.playheadSeconds() / (double) (ceo::kLayerSeconds * (layer == 6 ? 6.0f : 1.0f))));
    const float mx = x0 + width * markerPos;
    g.setColour (cream().withAlpha (0.9f));
    g.drawLine (mx, 4.0f, mx, bounds.getHeight() - 4.0f, 1.5f);

    // A small cream dot while recording.
    if (recording)
    {
        const float pulse = 0.55f + 0.45f * (float) std::sin (now * 6.0);
        g.setColour (cream().withAlpha (pulse));
        g.fillEllipse (x0 - 2.0f, 6.0f, 7.0f, 7.0f);
    }

    g.setColour (goldRim());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}
