#include "CeoControls.h"
#include "CeoLookAndFeel.h"

//==============================================================================
CeoKnob::CeoKnob()
    : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
{
    // 7:30 round to 4:30 (270 degrees), measured from 12 o'clock.
    setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    setMouseDragSensitivity (220);
    setScrollWheelEnabled (true);
}

double CeoKnob::snapValue (double attemptedValue, DragMode mode)
{
    if (centreDetent && mode != notDragging && std::abs (attemptedValue - 0.5) < 0.012)
        return 0.5;

    return attemptedValue;
}

//==============================================================================
namespace
{
    // Where the seven indicators sit, in degrees from 12 o'clock, clockwise (CeoGeo::socketAngle).
    constexpr float kS = LayerDial::kScale;
}

LayerDial::LayerDial (juce::RangedAudioParameter& layerParameter)
    : attachment (layerParameter,
                  [this] (float value)
                  {
                      const int index = juce::jlimit (0, 6, juce::roundToInt (value));
                      if (index != current)
                      {
                          current = index;
                          repaint();
                      }
                  })
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void LayerDial::setReducedMotion (bool shouldReduce)
{
    if (reducedMotion != shouldReduce)
    {
        reducedMotion = shouldReduce;
        repaint();
    }
}

void LayerDial::tickAnimation()
{
    if (reducedMotion)
        return;

    // Only the selected socket's halo moves, so only that corner is repainted.
    const juce::Point<float> p (CeoGeo::dialCx, CeoGeo::dialCy);
    const float a = juce::degreesToRadians (CeoGeo::socketAngle[current]);
    const float x = p.x + CeoGeo::socketRadius * std::sin (a), y = p.y - CeoGeo::socketRadius * std::cos (a);
    repaint (juce::Rectangle<float> (x - 48.0f, y - 48.0f, 96.0f, 96.0f).getSmallestIntegerContainer());
}

void LayerDial::paint (juce::Graphics& g)
{
    using namespace CeoStyle;

    const auto c = dialCentre();
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    // Cached: shadow, knurled ring, brass cap, the seven recessed sockets and the word "Layer".
    g.drawImage (art->dialBody (scale), juce::Rectangle<float> (0.0f, 0.0f, (float) CeoGeo::dialW, (float) CeoGeo::dialH));

    // Pointer towards the selected layer (dark groove with a bright line in it)
    {
        const float a = juce::degreesToRadians (CeoGeo::socketAngle[current]);
        const float s = std::sin (a), co = -std::cos (a);
        juce::Path pointer;
        pointer.startNewSubPath (c.x + 10.0f * s, c.y + 10.0f * co);
        pointer.lineTo          (c.x + 48.0f * s, c.y + 48.0f * co);
        g.setColour (juce::Colour (0xFF15100C));
        g.strokePath (pointer, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Breathing phase of the selected halo, 0..1 over 1.8 s
    float breath = 0.5f;
    if (! reducedMotion)
    {
        const double t = std::fmod (juce::Time::getMillisecondCounterHiRes(), 1800.0) / 1800.0;
        breath = (float) (0.5 - 0.5 * std::cos (t * juce::MathConstants<double>::twoPi));
    }

    // The seven jewels and their letters. Not selected: dim glass. Selected: full colour, white highlight, breathing halo.
    for (int i = 0; i < 7; ++i)
    {
        const bool selected = i == current;
        const auto p  = juce::Point<float> (c.x + CeoGeo::socketRadius * std::sin (juce::degreesToRadians (CeoGeo::socketAngle[i])),
                                            c.y - CeoGeo::socketRadius * std::cos (juce::degreesToRadians (CeoGeo::socketAngle[i])));
        const auto tp = juce::Point<float> (c.x + CeoGeo::letterRadius * std::sin (juce::degreesToRadians (CeoGeo::socketAngle[i])),
                                            c.y - CeoGeo::letterRadius * std::cos (juce::degreesToRadians (CeoGeo::socketAngle[i])));
        const auto colour = layerColour (i);

        if (selected)
        {
            const float size = 36.0f * (0.85f + 0.40f * breath);
            g.setOpacity (0.55f + 0.45f * breath);
            g.drawImage (art->halo (i), juce::Rectangle<float> (p.x - size, p.y - size, size * 2.0f, size * 2.0f));
            g.setColour (colour);
            g.fillEllipse (p.x - 8.5f, p.y - 8.5f, 17.0f, 17.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (p.x - 2.0f - 3.0f, p.y - 2.5f - 3.0f, 6.0f, 6.0f);
        }
        else
        {
            g.setColour (colour.withAlpha (0.28f));
            g.fillEllipse (p.x - 8.5f, p.y - 8.5f, 17.0f, 17.0f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillEllipse (p.x - 2.0f - 3.5f, p.y - 3.0f - 2.0f, 7.0f, 4.0f);
        }

        g.setColour (selected ? greekSelected() : greekDim());
        g.setFont (fontGreek (selected ? 23.0f : 17.0f));
        g.drawText (layerName (i), juce::Rectangle<float> (34.0f, 30.0f).withCentre (tp), juce::Justification::centred, false);
    }
}

void LayerDial::pickFromMouse (const juce::MouseEvent& e)
{
    const auto c = dialCentre();
    const float dx = e.position.x - c.x;
    const float dy = e.position.y - c.y;
    if (dx * dx + dy * dy < (10.0f * kS) * (10.0f * kS))
        return;                                         // too close to the middle to tell which way

    const float degrees = juce::radiansToDegrees (std::atan2 (dx, -dy));   // 0 = up, clockwise positive

    int best = 0;
    float bestDiff = 1.0e9f;
    for (int i = 0; i < 7; ++i)
    {
        float d = std::abs (degrees - CeoGeo::socketAngle[i]);
        d = juce::jmin (d, 360.0f - d);
        if (d < bestDiff)
        {
            bestDiff = d;
            best = i;
        }
    }

    if (best != current)
        attachment.setValueAsPartOfGesture ((float) best);
}

void LayerDial::mouseDown (const juce::MouseEvent& e)
{
    attachment.beginGesture();
    pickFromMouse (e);
}

void LayerDial::mouseDrag (const juce::MouseEvent& e)
{
    pickFromMouse (e);
}

void LayerDial::mouseUp (const juce::MouseEvent&)
{
    attachment.endGesture();
}

void LayerDial::step (int direction)
{
    const int next = juce::jlimit (0, 6, current + direction);
    if (next != current)
        attachment.setValueAsCompleteGesture ((float) next);
}

void LayerDial::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float delta = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);

    // Turning the wheel the other way starts counting again.
    if ((delta > 0.0f) != (wheelAccumulator > 0.0f) && wheelAccumulator != 0.0f)
        wheelAccumulator = 0.0f;

    wheelAccumulator += delta;

    // One click of an ordinary wheel steps one layer; a trackpad (smooth scrolling) needs a longer swipe.
    const float needed = wheel.isSmooth ? 0.7f : 0.15f;
    if (wheelAccumulator >= needed)
    {
        wheelAccumulator = 0.0f;
        step (1);
    }
    else if (wheelAccumulator <= -needed)
    {
        wheelAccumulator = 0.0f;
        step (-1);
    }
}
