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
    // Where the seven indicators sit, in degrees from 12 o'clock, clockwise.
    constexpr float kAngles[7] = { -135.0f, -90.0f, -45.0f, 0.0f, 45.0f, 90.0f, 135.0f };
    constexpr float kS               = LayerDial::kScale;
    constexpr float kK               = 1.0f + (kS - 1.0f) * 0.5f;      // dots, strokes and letters grow a little less than the dial
    constexpr float kDialRadius      = 30.0f * kS;
    constexpr float kIndicatorRadius = 46.0f * kS;
    constexpr float kLetterRadius    = 62.0f * kS;
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

void LayerDial::paint (juce::Graphics& g)
{
    using namespace CeoStyle;

    const auto c = dialCentre();

    // The dial itself, with a pointer towards the selected layer.
    g.setColour (knobFill());
    g.fillEllipse (c.x - kDialRadius, c.y - kDialRadius, kDialRadius * 2.0f, kDialRadius * 2.0f);
    g.setColour (goldRim());
    g.drawEllipse (c.x - kDialRadius, c.y - kDialRadius, kDialRadius * 2.0f, kDialRadius * 2.0f, 1.5f);

    const float a = juce::degreesToRadians (kAngles[current]);
    juce::Path pointer;
    pointer.startNewSubPath (c);
    pointer.lineTo (c.x + 18.0f * kS * std::sin (a), c.y - 18.0f * kS * std::cos (a));
    g.setColour (goldPointer());
    g.strokePath (pointer, juce::PathStrokeType (2.5f * kK, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The seven indicators and their letters.
    for (int i = 0; i < 7; ++i)
    {
        const float ang = juce::degreesToRadians (kAngles[i]);
        const float s = std::sin (ang), co = -std::cos (ang);
        const float r = ((i == current) ? 8.0f : 6.0f) * kK;
        const juce::Point<float> p (c.x + kIndicatorRadius * s, c.y + kIndicatorRadius * co);

        g.setColour (layerColour (i));
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        g.setColour (i == current ? selectedRing() : background());
        g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 1.5f * kK);

        g.setColour (lettering());
        g.setFont (font (13.0f * kK * 1.05f));
        g.drawText (layerName (i),
                    juce::Rectangle<float> (30.0f, 24.0f).withCentre ({ c.x + kLetterRadius * s, c.y + kLetterRadius * co }),
                    juce::Justification::centred, false);
    }

    g.setColour (lettering());
    g.setFont (font (12.5f * kK));
    g.drawText ("Layer", juce::Rectangle<float> (0.0f, c.y + kLetterRadius + 3.0f, (float) getWidth(), 24.0f),
                juce::Justification::centred, false);
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
        float d = std::abs (degrees - kAngles[i]);
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
