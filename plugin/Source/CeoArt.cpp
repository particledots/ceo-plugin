#include "CeoArt.h"

#include <BinaryData.h>
#include <cmath>
#include <vector>

namespace
{
    CeoArt* gCurrent = nullptr;

    juce::Colour blackA (float a) { return juce::Colours::black.withAlpha (a); }
    juce::Colour whiteA (float a) { return juce::Colours::white.withAlpha (a); }

    juce::Point<float> polar (float cx, float cy, float r, float degrees)
    {
        const float a = juce::degreesToRadians (degrees);
        return { cx + r * std::sin (a), cy - r * std::cos (a) };
    }

    // The soft dark pool under a round control: solid black 65% to 55% of the radius, then fading out.
    void softShadow (juce::Graphics& g, float cx, float cy, float r)
    {
        juce::ColourGradient grad (blackA (0.65f), cx, cy, blackA (0.0f), cx + r, cy, true);
        grad.addColour (0.55, blackA (0.65f));
        g.setGradientFill (grad);
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    }

    // The brass cap: radial gradient with the light coming from the upper left (as in the mockup).
    juce::ColourGradient brassCap (float cx, float cy, float r)
    {
        const float d = r * 2.0f;
        const float fx = cx - r + 0.38f * d, fy = cy - r + 0.30f * d;
        juce::ColourGradient grad (juce::Colour (0xFFC79A4A), fx, fy, juce::Colour (0xFF2C1A0A), fx + 0.9f * d, fy, true);
        grad.addColour (0.5, juce::Colour (0xFF6C4A20));
        return grad;
    }

    // Brass for keylines: light to dark along the diagonal of 'area'.
    juce::ColourGradient brassLine (juce::Rectangle<float> area, float alpha = 1.0f)
    {
        juce::ColourGradient grad (juce::Colour (0xFFF3DC96).withAlpha (alpha), area.getTopLeft(),
                                   juce::Colour (0xFF6E5020).withAlpha (alpha), area.getBottomRight(), false);
        grad.addColour (0.5, juce::Colour (0xFFB8903F).withAlpha (alpha));
        return grad;
    }

    // A ring of short lines (the knurling on knobs and the dial).
    void knurl (juce::Graphics& g, float cx, float cy, float rInner, float rOuter, int count, float width)
    {
        for (int t = 0; t < count; ++t)
        {
            const float deg = (float) t * 360.0f / (float) count;
            const auto a = polar (cx, cy, rInner, deg), b = polar (cx, cy, rOuter, deg);
            g.setColour (juce::Colour (0xFFD9B36B).withAlpha ((t % 2 == 0) ? 0.75f : 0.30f));
            g.drawLine (a.x, a.y, b.x, b.y, width);
        }
    }
}

//==============================================================================
CeoArt::CeoArt()
{
    // The fonts built into the plugin (see plugin/Fonts and plugin/CMakeLists.txt). To use other fonts,
    // put different files there (or change these six lines).
    jostRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::JostRegular_ttf,                  (size_t) BinaryData::JostRegular_ttfSize);
    jostMed     = juce::Typeface::createSystemTypefaceFor (BinaryData::JostMedium_ttf,                   (size_t) BinaryData::JostMedium_ttfSize);
    jostIt      = juce::Typeface::createSystemTypefaceFor (BinaryData::JostItalic_ttf,                   (size_t) BinaryData::JostItalic_ttfSize);
    titleFace   = juce::Typeface::createSystemTypefaceFor (BinaryData::CormorantGaramondSemiBoldLatin_ttf, (size_t) BinaryData::CormorantGaramondSemiBoldLatin_ttfSize);
    greekFace   = juce::Typeface::createSystemTypefaceFor (BinaryData::EBGaramondMediumGreek_ttf,       (size_t) BinaryData::EBGaramondMediumGreek_ttfSize);

    if (gCurrent == nullptr)
        gCurrent = this;
}

CeoArt::~CeoArt()
{
    if (gCurrent == this)
        gCurrent = nullptr;
}

CeoArt* CeoArt::current() noexcept
{
    return gCurrent;
}

static juce::Font makeFont (const juce::Typeface::Ptr& face, float size)
{
    if (face == nullptr)
        return juce::Font (juce::FontOptions (size));

    return juce::Font (juce::FontOptions (face).withPointHeight (size));
}

juce::Font CeoArt::jost (float size) const       { return makeFont (jostRegular, size); }
juce::Font CeoArt::jostMedium (float size) const { return makeFont (jostMed, size); }
juce::Font CeoArt::jostItalic (float size) const { return makeFont (jostIt, size); }
juce::Font CeoArt::title (float size) const      { return makeFont (titleFace, size); }
juce::Font CeoArt::greek (float size) const      { return makeFont (greekFace, size); }

float CeoArt::quantise (float physicalScale) noexcept
{
    return juce::jlimit (0.5f, 3.0f, std::round (physicalScale * 4.0f) / 4.0f);
}

const juce::Image& CeoArt::ensure (Cached& c, float physicalScale, int logicalW, int logicalH,
                                   const std::function<void (juce::Graphics&)>& draw)
{
    const float q = quantise (physicalScale);
    if (c.image.isNull() || c.scale != q)
    {
        const int pw = juce::jmax (1, juce::roundToInt ((float) logicalW * q));
        const int ph = juce::jmax (1, juce::roundToInt ((float) logicalH * q));
        juce::Image img (juce::Image::ARGB, pw, ph, true);
        {
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale ((float) pw / (float) logicalW, (float) ph / (float) logicalH));
            draw (g);
        }
        c.image = img;
        c.scale = q;
    }
    return c.image;
}

//==============================================================================
const juce::Image& CeoArt::knobBody (float ps)
{
    return ensure (knobBodyCache, ps, CeoGeo::knobBox, CeoGeo::knobBox, [this] (juce::Graphics& g) { paintKnobBody (g); });
}

const juce::Image& CeoArt::knobTicks (int lit, float ps)
{
    lit = juce::jlimit (0, 11, lit);
    return ensure (tickCache[lit], ps, CeoGeo::knobBox, CeoGeo::knobBox, [this, lit] (juce::Graphics& g) { paintKnobTicks (g, lit); });
}

const juce::Image& CeoArt::dialBody (float ps)
{
    return ensure (dialBodyCache, ps, CeoGeo::dialW, CeoGeo::dialH, [this] (juce::Graphics& g) { paintDialBody (g); });
}

const juce::Image& CeoArt::displayBack (float ps)
{
    return ensure (displayBackCache, ps, CeoGeo::displayW, CeoGeo::displayH, [this] (juce::Graphics& g) { paintDisplayBack (g); });
}

const juce::Image& CeoArt::displayOverlay (float ps)
{
    return ensure (displayOverlayCache, ps, CeoGeo::displayW, CeoGeo::displayH, [this] (juce::Graphics& g) { paintDisplayOverlay (g); });
}

const juce::Image& CeoArt::halo (int index)
{
    index = juce::jlimit (0, 7, index);
    auto& img = halos[(size_t) index];
    if (img.isNull())
    {
        img = juce::Image (juce::Image::ARGB, 64, 64, true);
        juce::Graphics g (img);
        juce::ColourGradient grad;
        if (index == 7)
        {
            grad = juce::ColourGradient (juce::Colour (0xFFFFE9A8).withAlpha (0.9f), 32.0f, 32.0f,
                                         juce::Colour (0xFFE3BE6A).withAlpha (0.0f), 64.0f, 32.0f, true);
        }
        else
        {
            const auto c = index == 6 ? CeoStyle::cream() : CeoStyle::layerColour (index);
            grad = juce::ColourGradient (c.withAlpha (0.95f), 32.0f, 32.0f, c.withAlpha (0.0f), 64.0f, 32.0f, true);
            grad.addColour (0.35, c.withAlpha (0.35f));
        }
        g.setGradientFill (grad);
        g.fillRect (0, 0, 64, 64);
    }
    return img;
}

const juce::Image& CeoArt::noiseTile()
{
    if (noise.isNull())
    {
        noise = juce::Image (juce::Image::ARGB, 256, 256, true);
        juce::Image::BitmapData bd (noise, juce::Image::BitmapData::readWrite);
        juce::Random rnd (9);
        for (int y = 0; y < 256; ++y)
        {
            auto* line = (juce::PixelARGB*) bd.getLinePointer (y);
            for (int x = 0; x < 256; ++x)
            {
                // Like the mockup's fractal noise: alpha = 0.9 * noise - 0.3, doubled so the grain can be seen at 10%.
                const float n = (rnd.nextFloat() + rnd.nextFloat()) * 0.5f;
                const float a = juce::jlimit (0.0f, 1.0f, (0.9f * n - 0.3f) * 2.0f);
                juce::PixelARGB p ((juce::uint8) juce::roundToInt (a * 255.0f), 230, 179, 102);
                p.premultiply();
                line[x] = p;
            }
        }
    }
    return noise;
}

//==============================================================================
void CeoArt::paintKnobBody (juce::Graphics& g)
{
    const float cx = CeoGeo::knobCentre, cy = CeoGeo::knobCentre;

    softShadow (g, cx + 2.0f, cy + 5.0f, 34.0f);

    // Knurled ring
    g.setColour (juce::Colour (0xFF2B1A0B));
    g.fillEllipse (cx - 29.0f, cy - 29.0f, 58.0f, 58.0f);
    g.setColour (blackA (0.6f));
    g.drawEllipse (cx - 29.0f, cy - 29.0f, 58.0f, 58.0f, 1.0f);
    knurl (g, cx, cy, 26.5f, 29.0f, 48, 1.1f);

    // Brass cap with faint machined rings and a soft highlight
    g.setGradientFill (brassCap (cx, cy, 22.0f));
    g.fillEllipse (cx - 22.0f, cy - 22.0f, 44.0f, 44.0f);
    g.setColour (blackA (0.5f));
    g.drawEllipse (cx - 22.0f, cy - 22.0f, 44.0f, 44.0f, 1.0f);
    g.setColour (whiteA (0.09f));
    g.drawEllipse (cx - 15.0f, cy - 15.0f, 30.0f, 30.0f, 1.0f);
    g.setColour (blackA (0.25f));
    g.drawEllipse (cx - 9.0f, cy - 9.0f, 18.0f, 18.0f, 1.0f);
    g.setColour (whiteA (0.12f));
    g.fillEllipse (cx - 4.0f - 14.0f, cy - 12.0f - 7.0f, 28.0f, 14.0f);
}

void CeoArt::paintKnobTicks (juce::Graphics& g, int lit)
{
    const float cx = CeoGeo::knobCentre, cy = CeoGeo::knobCentre;

    // 11 ticks over the 270 degree sweep, every 5th one longer. The ones up to the value glow.
    for (int t = 0; t < 11; ++t)
    {
        const float deg = 225.0f + (float) t * 27.0f;
        const bool isLit = t < lit;
        const bool major = (t % 5) == 0;
        const auto a = polar (cx, cy, 34.0f, deg);
        const auto b = polar (cx, cy, major ? 40.0f : 38.0f, deg);

        if (isLit)
        {
            // glow: a few wide, faint strokes under the crisp one
            for (const auto& s : { std::pair<float, float> { 8.0f, 0.10f }, { 6.0f, 0.16f }, { 4.0f, 0.26f } })
            {
                g.setColour (CeoStyle::glow().withAlpha (s.second));
                g.drawLine (a.x, a.y, b.x, b.y, s.first);
            }
        }

        g.setColour (isLit ? CeoStyle::litTick() : CeoStyle::dimTick());
        juce::Path line;
        line.startNewSubPath (a);
        line.lineTo (b);
        g.strokePath (line, juce::PathStrokeType (major ? 2.0f : 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void CeoArt::paintDialBody (juce::Graphics& g)
{
    using namespace CeoGeo;
    const float cx = dialCx, cy = dialCy;

    softShadow (g, cx + 2.0f, cy + 8.0f, 84.0f);

    // Housing and the 96-tooth ring
    g.setColour (juce::Colour (0xFF241408));
    g.fillEllipse (cx - 66.0f, cy - 66.0f, 132.0f, 132.0f);
    g.setColour (blackA (0.7f));
    g.drawEllipse (cx - 66.0f, cy - 66.0f, 132.0f, 132.0f, 2.0f);
    knurl (g, cx, cy, 59.0f, 64.0f, 96, 1.3f);

    // Brass cap
    g.setGradientFill (brassCap (cx, cy, 52.0f));
    g.fillEllipse (cx - 52.0f, cy - 52.0f, 104.0f, 104.0f);
    g.setColour (blackA (0.5f));
    g.drawEllipse (cx - 52.0f, cy - 52.0f, 104.0f, 104.0f, 1.0f);
    for (float r : { 40.0f, 28.0f, 16.0f })
    {
        g.setColour (whiteA (0.07f));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
        g.setColour (blackA (0.2f));
        g.drawEllipse (cx - r, cy - r + 1.0f, r * 2.0f, r * 2.0f, 1.0f);
    }
    g.setColour (whiteA (0.10f));
    g.fillEllipse (cx - 6.0f - 34.0f, cy - 26.0f - 15.0f, 68.0f, 30.0f);

    // Seven recessed sockets for the layer "jewels"
    for (int i = 0; i < 7; ++i)
    {
        const auto p = polar (cx, cy, socketRadius, socketAngle[i]);
        g.setColour (juce::Colour (0xFF120A05));
        g.fillEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f);
        const juce::Rectangle<float> box (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f);
        g.setGradientFill (brassLine (box));
        g.drawEllipse (box, 2.0f);
    }

    g.setColour (CeoStyle::lettering());
    g.setFont (jost (14.0f).withExtraKerningFactor (1.5f / 14.0f));
    g.drawSingleLineText ("Layer", juce::roundToInt (cx), juce::roundToInt (cy + 148.0f), juce::Justification::horizontallyCentred);
}

void CeoArt::paintDisplayBack (juce::Graphics& g)
{
    using namespace CeoGeo;
    const juce::Rectangle<float> all (0.0f, 0.0f, (float) displayW, (float) displayH);
    const juce::Rectangle<float> glass ((float) displayMargin, (float) displayMargin, (float) glassW, (float) glassH);

    // Thick black bezel with a brass keyline
    g.setColour (juce::Colour (0xFF0A0503));
    g.fillRoundedRectangle (all, 12.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (all.reduced (0.5f), 12.0f, 1.0f);
    g.setGradientFill (brassLine (all, 0.85f));
    g.drawRoundedRectangle (all.reduced (1.2f), 11.5f, 1.6f);

    // Near-black glass, a little lighter in the middle (an ellipse, like the mockup's radial gradient)
    juce::ColourGradient crt (juce::Colour (0xFF0E0A07), 0.0f, 0.0f, juce::Colour (0xFF000000), 1.0f, 0.0f, true);
    crt.addColour (0.75, juce::Colour (0xFF050302));
    juce::FillType fill (crt);
    fill.transform = juce::AffineTransform::scale (0.75f * (float) glassW, 0.75f * (float) glassH)
                         .translated (glass.getCentreX(), glass.getCentreY());
    g.setFillType (fill);
    g.fillRoundedRectangle (glass, 9.0f);
}

void CeoArt::paintDisplayOverlay (juce::Graphics& g)
{
    using namespace CeoGeo;
    const juce::Rectangle<float> glass ((float) displayMargin, (float) displayMargin, (float) glassW, (float) glassH);
    juce::Path glassPath;
    glassPath.addRoundedRectangle (glass, 9.0f);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (glassPath, juce::AffineTransform());

    // Scan lines: 1 unit of black every 3 units
    g.setColour (blackA (0.35f));
    for (float y = glass.getY(); y < glass.getBottom(); y += 3.0f)
        g.fillRect (glass.getX(), y, glass.getWidth(), 1.0f);

    // Curved-glass sheen from the top left
    juce::ColourGradient sheen (whiteA (0.12f), glass.getX(), glass.getY(), whiteA (0.0f), glass.getRight(), glass.getBottom(), false);
    sheen.addColour (0.4, whiteA (0.0f));
    g.setGradientFill (sheen);
    g.fillRect (glass);

    // Dark inner shadow all round the glass
    g.setColour (blackA (0.8f));
    g.drawRoundedRectangle (glass, 9.0f, 3.0f);
}

//==============================================================================
void CeoArt::paintGround (juce::Graphics& g, float physicalScale)
{
    const juce::Rectangle<float> r (0.0f, 0.0f, 784.0f, 672.0f);
    juce::Path panel;
    panel.addRoundedRectangle (r, 14.0f);

    g.setGradientFill (juce::ColourGradient (CeoStyle::panelTop(), 0.0f, 0.0f, CeoStyle::panelBottom(), 0.0f, 672.0f, false));
    g.fillPath (panel);

    // Leather-style grain, one tile pixel per screen pixel, at 10%
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (panel, juce::AffineTransform());
        g.setFillType (juce::FillType (noiseTile(), juce::AffineTransform::scale (1.0f / physicalScale)));
        g.setOpacity (0.10f);
        g.fillRect (r);
    }

    // Edge, top highlight, bottom shadow, inner bevel
    g.setColour (CeoStyle::border());
    g.drawRoundedRectangle (1.0f, 1.0f, 782.0f, 670.0f, 13.0f, 2.0f);
    g.setColour (juce::Colour (0xFFD9B36B).withAlpha (0.55f));
    g.drawLine (14.0f, 2.0f, 770.0f, 2.0f, 1.0f);
    g.setColour (blackA (0.6f));
    g.drawLine (14.0f, 670.0f, 770.0f, 670.0f, 1.0f);
    g.setColour (blackA (0.55f));
    g.drawRoundedRectangle (12.5f, 12.5f, 759.0f, 647.0f, 8.0f, 1.0f);
    g.setColour (juce::Colour (0xFFC9A24B).withAlpha (0.25f));
    g.drawRoundedRectangle (13.5f, 13.5f, 759.0f, 647.0f, 8.0f, 1.0f);

    // Four slotted brass screws
    struct Screw { float x, y, rot; };
    for (const auto& s : { Screw { 26.0f, 26.0f, 30.0f }, Screw { 758.0f, 26.0f, 100.0f }, Screw { 26.0f, 646.0f, 70.0f }, Screw { 758.0f, 646.0f, 150.0f } })
    {
        g.setGradientFill (brassCap (s.x, s.y, 7.0f));
        g.fillEllipse (s.x - 7.0f, s.y - 7.0f, 14.0f, 14.0f);
        g.setColour (blackA (0.6f));
        g.drawEllipse (s.x - 7.0f, s.y - 7.0f, 14.0f, 14.0f, 1.0f);
        const float c = std::cos (juce::degreesToRadians (s.rot)), sn = std::sin (juce::degreesToRadians (s.rot));
        g.setColour (juce::Colour (0xFF1A0E06));
        g.drawLine (s.x - 5.0f * c, s.y - 5.0f * sn, s.x + 5.0f * c, s.y + 5.0f * sn, 2.0f);
    }

    // The title: gold gradient with a thin dark outline
    {
        const auto f = title (38.0f).withExtraKerningFactor (4.0f / 38.0f);
        const auto text = juce::String::fromUTF8 ("Ce\xc3\xb2");
        const float w = juce::GlyphArrangement::getStringWidth (f, text);
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, text, 392.0f - w * 0.5f, 46.0f);
        juce::Path p;
        ga.createPath (p);
        const auto bb = p.getBounds();
        g.setGradientFill (brassLine (bb));
        g.fillPath (p);
        g.setColour (blackA (0.5f));
        g.strokePath (p, juce::PathStrokeType (0.6f));
    }
}

//==============================================================================
namespace CeoStyle
{
    juce::Font font (float size)
    {
        if (auto* art = CeoArt::current()) return art->jost (size);
        return juce::Font (juce::FontOptions (size));
    }

    juce::Font fontMedium (float size)
    {
        if (auto* art = CeoArt::current()) return art->jostMedium (size);
        return juce::Font (juce::FontOptions (size));
    }

    juce::Font fontItalic (float size)
    {
        if (auto* art = CeoArt::current()) return art->jostItalic (size);
        return juce::Font (juce::FontOptions (size).withStyle ("Italic"));
    }

    juce::Font fontTitle (float size)
    {
        if (auto* art = CeoArt::current()) return art->title (size);
        return juce::Font (juce::FontOptions (size).withStyle ("Bold"));
    }

    juce::Font fontGreek (float size)
    {
        if (auto* art = CeoArt::current()) return art->greek (size);
        return juce::Font (juce::FontOptions (size));
    }

    void drawMixedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                        const juce::Font& latin, const juce::Font& greekFont)
    {
        struct Run { juce::String s; bool isGreek; };
        std::vector<Run> runs;
        for (auto it = text.getCharPointer(); ! it.isEmpty();)
        {
            const auto c = it.getAndAdvance();
            const bool gk = c >= 0x370 && c <= 0x3ff;
            if (runs.empty() || runs.back().isGreek != gk)
                runs.push_back ({ juce::String(), gk });
            runs.back().s += juce::String::charToString (c);
        }

        auto widthOf = [&]
        {
            float w = 0.0f;
            for (const auto& r : runs)
                w += juce::GlyphArrangement::getStringWidth (r.isGreek ? greekFont : latin, r.s);
            return w;
        };

        if (widthOf() > area.getWidth())
        {
            const auto ellipsis = juce::String::charToString (0x2026);
            const float ellipsisWidth = juce::GlyphArrangement::getStringWidth (latin, ellipsis);
            while (! runs.empty() && widthOf() + ellipsisWidth > area.getWidth())
            {
                runs.back().s = runs.back().s.dropLastCharacters (1);
                if (runs.back().s.isEmpty())
                    runs.pop_back();
            }
            runs.push_back ({ ellipsis, false });
        }

        float x = area.getCentreX() - widthOf() * 0.5f;
        const float baseline = area.getCentreY() + (latin.getAscent() - latin.getDescent()) * 0.5f;
        for (const auto& r : runs)
        {
            const auto& f = r.isGreek ? greekFont : latin;
            juce::GlyphArrangement ga;
            ga.addLineOfText (f, r.s, x, baseline);
            ga.draw (g);
            x += juce::GlyphArrangement::getStringWidth (f, r.s);
        }
    }

    bool prefersReducedMotion()
    {
        return ceoSystemPrefersReducedMotion();
    }
}
