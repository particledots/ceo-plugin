#include "StreamDisplay.h"
#include "CeoLookAndFeel.h"
#include "Parameters.h"

#include <ceo/Mapping.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

StreamDisplay::StreamDisplay (const ceo::Engine& engineToShow, juce::AudioProcessorValueTreeState& state)
    : engine (engineToShow)
{
    layerValue  = state.getRawParameterValue (CeoParams::layer);
    scanValue   = state.getRawParameterValue (CeoParams::scan);
    sprayValue  = state.getRawParameterValue (CeoParams::spray);
    lengthValue = state.getRawParameterValue (CeoParams::length);
    followValue = state.getRawParameterValue (CeoParams::follow);

    setInterceptsMouseClicks (false, false);
}

StreamDisplay::Geo StreamDisplay::geometry() const
{
    Geo geo;
    geo.glass = getLocalBounds().toFloat().reduced ((float) CeoGeo::displayMargin);
    geo.x0    = geo.glass.getX() + 12.0f;
    geo.width = geo.glass.getWidth() - 24.0f;
    geo.midY  = geo.glass.getY() + geo.glass.getHeight() * 0.548f;
    geo.scale = geo.glass.getHeight() / 84.0f;          // the mockup display is 84 high
    return geo;
}

void StreamDisplay::rebuildRibbon (int layer)
{
    float next[kRibbonPoints] = {};
    bool ok = true;
    if (layer < 6)
    {
        ok = engine.layerEnvelope (layer, next, kRibbonPoints);
    }
    else
    {
        const int perLayer = kRibbonPoints / 6;
        for (int l = 0; l < 6 && ok; ++l)
            ok = engine.layerEnvelope (l, next + l * perLayer, perLayer);
    }

    if (ok)
    {
        // The ribbon picture is only redrawn when something it shows has changed.
        if (layer != shownLayer || std::memcmp (next, ribbon, sizeof (ribbon)) != 0)
            ribbonDirty = true;

        std::memcpy (ribbon, next, sizeof (ribbon));
        shownLayer = layer;
        shownRevision = engine.recordingRevision();
        ticksSinceRibbon = 0;
    }
}

int StreamDisplay::colourIndexAt (float position, int layer) const
{
    if (layer < 6)
        return layer;

    return juce::jlimit (0, 5, (int) (position * 6.0f));
}

float StreamDisplay::markerPosition (int layer) const
{
    // The Scan position, or in Follow mode the playhead.
    float pos = juce::jlimit (0.0f, 1.0f, scanValue->load());
    if (followValue->load() > 0.5f)
        pos = juce::jlimit (0.0f, 1.0f, (float) (engine.playheadSeconds() / (double) (ceo::kLayerSeconds * (layer == 6 ? 6.0f : 1.0f))));
    return pos;
}

float StreamDisplay::ribbonHalfHeight (int index, float scale) const
{
    // Loudness (RMS) to thickness: a full-scale-ish sound fills the display, quiet sound still shows,
    // an empty layer is a thin line.
    const float level = juce::jlimit (0.0f, 1.0f, ribbon[index] * 3.0f);
    return scale * (3.0f + 25.0f * std::pow (level, 0.6f));
}

void StreamDisplay::addSpark (const ceo::GrainStart& gs, int layer, float spray, float grainSeconds, double now)
{
    const auto geo = geometry();
    const int idx = juce::jlimit (0, kRibbonPoints - 1, (int) std::lround (gs.position * (float) (kRibbonPoints - 1)));
    const float half = ribbonHalfHeight (idx, geo.scale);
    const bool below = random.nextFloat() < 0.22f;

    Spark s;
    s.x = geo.x0 + gs.position * geo.width + (random.nextFloat() - 0.5f) * 3.0f;
    s.y = below ? geo.midY + half * 0.55f : geo.midY - half;

    // Spray = how far it is thrown: low hugs the ribbon, high throws it wide. Up from the top edge, down from the lower one.
    const float spread = geo.scale * (4.0f + spray * 34.0f);
    const float dist = spread * (0.55f + 0.45f * random.nextFloat()) * (gs.strike ? 1.6f : 1.0f);
    const float angle = juce::degreesToRadians (below ? 90.0f + (random.nextFloat() - 0.5f) * 60.0f
                                                      : -90.0f + (random.nextFloat() - 0.5f) * 70.0f);
    s.dx = std::cos (angle) * dist;
    s.dy = std::sin (angle) * dist;

    s.size       = (0.6f + random.nextFloat() * 1.1f) * (gs.strike ? 1.8f : 1.0f);
    s.brightness = 0.55f + random.nextFloat() * 0.45f;
    s.life       = juce::jlimit (0.35f, 3.0f, grainSeconds);          // follows the real grain length
    s.born       = now;
    s.colour     = colourIndexAt (gs.position, layer);
    s.strike     = gs.strike;

    if (reducedMotion)
    {
        s.dx = s.dy = 0.0f;
        s.life = juce::jmin (s.life, 1.6f);
    }

    if (sparkCount < kMaxSparks)
    {
        sparks[(size_t) sparkCount++] = s;
    }
    else
    {
        // full: the oldest one makes room
        int oldest = 0;
        for (int i = 1; i < kMaxSparks; ++i)
            if (sparks[(size_t) i].born < sparks[(size_t) oldest].born)
                oldest = i;
        sparks[(size_t) oldest] = s;
    }
}

void StreamDisplay::tick()
{
    const int layer = juce::jlimit (0, 6, juce::roundToInt (layerValue->load()));
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    bool changed = false;

    // Rebuild the ribbon when the layer dial moved or the recording changed (at most every 4th tick, about 8 times a
    // second, while recording), and now and then anyway. The picture is only redrawn if the numbers differ.
    ++ticksSinceRibbon;
    if (layer != shownLayer
        || (engine.recordingRevision() != shownRevision && ticksSinceRibbon >= 4)
        || ticksSinceRibbon >= 30)
        rebuildRibbon (layer);
    changed = changed || ribbonDirty;

    // New grains become sparks.
    ceo::GrainStart starts[64];
    const int count = engine.takeGrainStarts (grainCursor, starts, 64);
    const float spray = sprayValue->load();
    const float grainSeconds = ceo::map::grainLengthSeconds (lengthValue->load());
    for (int i = juce::jmax (0, count - kMaxSparks); i < count; ++i)
        addSpark (starts[i], layer, spray, grainSeconds, now);

    for (int i = 0; i < count; ++i)
    {
        if (starts[i].reversed)
            reverseLevel = 1.0f;
        else
            forwardLevel = 1.0f;
    }

    // Spent sparks go (swap with the last one).
    for (int i = 0; i < sparkCount;)
    {
        if (now - sparks[(size_t) i].born >= (double) sparks[(size_t) i].life)
            sparks[(size_t) i] = sparks[(size_t) --sparkCount];
        else
            ++i;
    }
    changed = changed || sparkCount > 0 || count > 0;

    forwardLevel *= 0.72f;
    reverseLevel *= 0.72f;

    const bool wasRecording = recording;
    recording = engine.isRecording();
    changed = changed || recording || wasRecording;

    const float marker = markerPosition (layer);
    if (std::abs (marker - lastMarker) > 0.0004f)
    {
        lastMarker = marker;
        changed = true;
    }

    if (changed)
        repaint();
}

//==============================================================================
void StreamDisplay::renderRibbon (float scale)
{
    using namespace CeoStyle;

    const auto geo = geometry();
    const float W = (float) getWidth(), H = (float) getHeight();
    const int pw = juce::jmax (1, juce::roundToInt (W * scale));
    const int ph = juce::jmax (1, juce::roundToInt (H * scale));
    const auto toPhysical = juce::AffineTransform::scale ((float) pw / W, (float) ph / H);
    const int layer = juce::jlimit (0, 6, shownLayer < 0 ? 0 : shownLayer);

    // Outline: top edge = middle line minus the thickness, bottom edge = a little under the middle line.
    auto xAt = [&] (int i) { return geo.x0 + geo.width * (float) i / (float) (kRibbonPoints - 1); };
    juce::Path body, top, lower, head;
    for (int i = 0; i < kRibbonPoints; ++i)
    {
        const float h = ribbonHalfHeight (i, geo.scale);
        if (i == 0)
        {
            body.startNewSubPath (xAt (i), geo.midY - h);
            top.startNewSubPath  (xAt (i), geo.midY - h);
            head.startNewSubPath (xAt (i), geo.midY - h - 6.0f * geo.scale);
        }
        else
        {
            body.lineTo (xAt (i), geo.midY - h);
            top.lineTo  (xAt (i), geo.midY - h);
            head.lineTo (xAt (i), geo.midY - h - 6.0f * geo.scale);
        }
    }
    for (int i = kRibbonPoints - 1; i >= 0; --i)
    {
        const float low = geo.midY + ribbonHalfHeight (i, geo.scale) * 0.55f;
        body.lineTo (xAt (i), low);
        if (i == kRibbonPoints - 1)
            lower.startNewSubPath (xAt (i), low);
        else
            lower.lineTo (xAt (i), low);
    }
    body.closeSubPath();

    // One colour, or for Omega all six layer colours side by side.
    auto fillFor = [&] (float alpha)
    {
        if (layer < 6)
            return juce::FillType (layerColour (layer).withAlpha (alpha));

        juce::ColourGradient gradient (layerColour (0).withAlpha (alpha), geo.x0, 0.0f, layerColour (5).withAlpha (alpha), geo.x0 + geo.width, 0.0f, false);
        for (int s = 1; s < 5; ++s)
            gradient.addColour (((double) s + 0.5) / 6.0, layerColour (s).withAlpha (alpha));
        return juce::FillType (gradient);
    };

    // 1. Soft glow: the ribbon drawn small, blurred, and stretched back up (much cheaper than blurring it full size).
    const int sw = juce::jmax (1, pw / 4), sh = juce::jmax (1, ph / 4);
    juce::Image small (juce::Image::ARGB, sw, sh, true, juce::SoftwareImageType());
    {
        juce::Graphics sg (small);
        sg.addTransform (juce::AffineTransform::scale ((float) sw / W, (float) sh / H));
        sg.setFillType (fillFor (1.0f));
        sg.fillPath (body);
    }
    const float sigma = juce::jmax (0.8f, 6.0f * scale / 4.0f);
    const int kernelSize = 2 * (int) std::ceil (sigma * 2.0f) + 1;
    juce::ImageConvolutionKernel blur (kernelSize);
    blur.createGaussianBlur (sigma);
    juce::Image blurred = small.createCopy();
    blur.applyToImage (blurred, small, blurred.getBounds());

    // 2. Glow at 50% and the ribbon body at 80%, then fade both towards the bottom of the glass (bright top edge).
    juce::Image layerImage (juce::Image::ARGB, pw, ph, true, juce::SoftwareImageType());
    {
        juce::Graphics lg (layerImage);
        lg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        lg.setOpacity (0.5f);
        lg.drawImage (blurred, juce::Rectangle<float> (0.0f, 0.0f, (float) pw, (float) ph));
        lg.addTransform (toPhysical);
        lg.setFillType (fillFor (0.8f));
        lg.fillPath (body);
    }
    {
        juce::Image::BitmapData bd (layerImage, juce::Image::BitmapData::readWrite);
        const float top0 = geo.glass.getY(), height = geo.glass.getHeight();
        for (int y = 0; y < ph; ++y)
        {
            const float t = juce::jlimit (0.0f, 1.0f, ((float) y / scale - top0) / height);
            const float factor = 1.0f - 0.835f * t;                      // white at the top to #2a2a2a at the bottom
            auto* line = (juce::PixelARGB*) bd.getLinePointer (y);
            for (int x = 0; x < pw; ++x)
                line[x].multiplyAlpha (factor);
        }
    }

    // 3. The finished picture: layer + edge glow, bright top edge, cream lower edge, dashed play line.
    juce::Image out (juce::Image::ARGB, pw, ph, true);
    {
        juce::Graphics og (out);
        og.drawImageAt (layerImage, 0, 0);
        og.addTransform (toPhysical);

        for (const auto& s : { std::pair<float, float> { 7.0f, 0.16f }, { 5.0f, 0.28f }, { 3.2f, 0.42f } })
        {
            og.setFillType (fillFor (s.second));
            og.strokePath (top, juce::PathStrokeType (s.first, juce::PathStrokeType::curved));
        }

        og.setColour (juce::Colour (0xFFF4FFF8).withAlpha (0.95f));
        og.strokePath (top, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved));

        og.setColour (cream().withAlpha (0.7f));
        og.strokePath (lower, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, head, dashes, 2);
        og.setColour (cream().withAlpha (0.8f));
        og.fillPath (dashed);
    }

    ribbonImage = out;
    ribbonScale = scale;
    ribbonDirty = false;
    ++ribbonRenderCount;
}

void StreamDisplay::drawSparks (juce::Graphics& g, double now)
{
    using namespace CeoStyle;

    if (sparkCount == 0)
        return;

    const auto geo = geometry();
    const float minY = geo.glass.getY() + 4.0f, maxY = geo.glass.getBottom() - 4.0f;

    for (int i = 0; i < sparkCount; ++i)
    {
        const auto& s = sparks[(size_t) i];
        const float age = (float) (now - s.born);
        if (age < 0.0f || age >= s.life)
            continue;

        const float u = age / s.life;
        const float alpha = juce::jmin (1.0f, age / 0.03f) * std::pow (1.0f - u, 1.4f) * s.brightness;
        const float ease = 1.0f - (1.0f - u) * (1.0f - u);                 // fast at first, slowing down
        const float px = s.x + s.dx * ease;
        const float py = juce::jlimit (minY, maxY, s.y + s.dy * ease);
        const float flash = reducedMotion ? 0.0f : std::exp (-age / 0.10f);   // the white-hot pop at the start
        const auto colour = s.strike ? cream() : layerColour (s.colour);

        // Afterglow: a faint dot stays where the grain started, and fades with the flash.
        const float ghost = juce::jmax (0.0f, 1.0f - age / juce::jmin (s.life, 0.9f));
        if (ghost > 0.0f)
        {
            const float gr = s.size * 1.2f;
            g.setColour (colour.withAlpha (0.55f * ghost * s.brightness));
            g.fillEllipse (s.x - gr, juce::jlimit (minY, maxY, s.y) - gr, gr * 2.0f, gr * 2.0f);
        }

        // Tail pointing back along the path
        const float length = std::hypot (s.dx, s.dy);
        if (! reducedMotion && length > 0.5f)
        {
            const float ux = s.dx / length, uy = s.dy / length;
            const float tail = (8.0f + s.size * 5.0f) * (1.0f - 0.5f * u) * juce::jmin (1.0f, 0.2f + age / 0.08f);
            const float hw = s.size * 0.8f;
            tailPath.clear();
            tailPath.startNewSubPath (px - uy * hw, py + ux * hw);
            tailPath.lineTo          (px - ux * tail, py - uy * tail);
            tailPath.lineTo          (px + uy * hw, py - ux * hw);
            tailPath.closeSubPath();
            g.setColour (colour.withAlpha (0.55f * alpha));
            g.fillPath (tailPath);
        }

        // Halo sprite (bigger during the flash), then the core: white-hot first, cooling to the layer colour.
        const float hr = s.size * 5.5f * (1.0f + 1.7f * flash);
        g.setOpacity (juce::jlimit (0.0f, 1.0f, alpha * 0.9f));
        g.drawImage (art->halo (s.strike ? 6 : s.colour), juce::Rectangle<float> (px - hr, py - hr, hr * 2.0f, hr * 2.0f));

        const float cr = s.size * (0.8f + 0.5f * flash);
        g.setColour (colour.interpolatedWith (juce::Colours::white, flash).withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
        g.fillEllipse (px - cr, py - cr, cr * 2.0f, cr * 2.0f);
    }
}

void StreamDisplay::paint (juce::Graphics& g)
{
    using namespace CeoStyle;

    const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float quantised = CeoArt::quantise (physical);
    const auto all = getLocalBounds().toFloat();
    const auto geo = geometry();
    const int layer = juce::jlimit (0, 6, juce::roundToInt (layerValue->load()));
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    g.drawImage (art->displayBack (physical), all);                  // bezel and glass

    if (ribbonDirty || ribbonImage.isNull() || ribbonScale != quantised)
        renderRibbon (quantised);
    g.drawImage (ribbonImage, all);                                  // ribbon with its glow, edges and dashed play line

    drawSparks (g, now);

    // The Scan marker: an amber needle with a soft glow (or, in Follow mode, the playhead).
    {
        const float mx = geo.x0 + geo.width * markerPosition (layer);
        const float y0 = geo.glass.getY() + 4.0f, y1 = geo.glass.getBottom() - 4.0f;
        for (const auto& s : { std::pair<float, float> { 9.0f, 0.10f }, { 6.0f, 0.18f }, { 3.6f, 0.30f } })
        {
            g.setColour (juce::Colour (0xFFFFB84D).withAlpha (s.second));
            g.drawLine (mx, y0, mx, y1, s.first);
        }
        g.setColour (amberNeedle());
        g.drawLine (mx, y0, mx, y1, 1.6f);
    }

    // A small cream dot while recording.
    if (recording)
    {
        const float pulse = reducedMotion ? 0.8f : 0.55f + 0.45f * (float) std::sin (now * 6.0);
        g.setColour (cream().withAlpha (pulse));
        g.fillEllipse (geo.x0 - 2.0f, geo.glass.getY() + 6.0f, 7.0f, 7.0f);
    }

    g.drawImage (art->displayOverlay (physical), all);               // scan lines, glass sheen, inner shadow
}
