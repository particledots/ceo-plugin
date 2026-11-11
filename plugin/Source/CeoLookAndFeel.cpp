#include "CeoLookAndFeel.h"

#include <cmath>

CeoLookAndFeel::CeoLookAndFeel()
{
    using namespace CeoStyle;

    setColour (juce::ComboBox::backgroundColourId,         juce::Colour (0xFF120A05));
    setColour (juce::ComboBox::textColourId,               menuText());
    setColour (juce::ComboBox::outlineColourId,            goldRim());
    setColour (juce::ComboBox::arrowColourId,              lamp());
    setColour (juce::ComboBox::focusedOutlineColourId,     goldPointer());

    setColour (juce::PopupMenu::backgroundColourId,            background());
    setColour (juce::PopupMenu::textColourId,                  menuText());
    setColour (juce::PopupMenu::headerTextColourId,            lettering());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, goldRim());
    setColour (juce::PopupMenu::highlightedTextColourId,       background());

    // The value bubble shown while a knob is dragged.
    setColour (juce::BubbleComponent::backgroundColourId,  displayBackground());
    setColour (juce::BubbleComponent::outlineColourId,     goldRim());
    setColour (juce::TooltipWindow::textColourId,          menuText());
}

void CeoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    using namespace CeoStyle;

    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto centre = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).getCentre();
    const juce::Rectangle<float> box (centre.x - CeoGeo::knobCentre, centre.y - CeoGeo::knobCentre, (float) CeoGeo::knobBox, (float) CeoGeo::knobBox);

    // Cached: shadow, knurled ring, brass cap (the same for every knob) and the scale with its lit part.
    g.drawImage (art->knobBody (scale), box);
    const int lit = juce::jlimit (1, 11, (int) std::floor (sliderPos * 10.0f + 0.0185f) + 1);
    g.drawImage (art->knobTicks (lit, scale), box);

    // Only this is drawn live: a faint ring while the mouse is over or on the knob, and the pointer.
    if (slider.isMouseOverOrDragging())
    {
        g.setColour (glow().withAlpha (slider.isMouseButtonDown() ? 0.60f : 0.28f));
        g.drawEllipse (centre.x - 30.5f, centre.y - 30.5f, 61.0f, 61.0f, 1.2f);
    }

    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float s = std::sin (angle), c = -std::cos (angle);
    juce::Path pointer;
    pointer.startNewSubPath (centre.x + s * 5.0f,  centre.y + c * 5.0f);
    pointer.lineTo          (centre.x + s * 22.0f, centre.y + c * 22.0f);
    g.setColour (juce::Colour (0xFF15100C));
    g.strokePath (pointer, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CeoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    using namespace CeoStyle;

    const bool lit = button.getToggleState() || shouldDrawButtonAsDown;
    const float alpha = button.isEnabled() ? 1.0f : 0.4f;               // greyed out when it cannot be used now
    const float radius = 5.0f;

    // The body leaves 2 units at the bottom for the drop shadow (a pressed button sinks into it).
    auto body = button.getLocalBounds().toFloat().reduced (0.5f).withTrimmedBottom (2.0f);
    if (shouldDrawButtonAsDown)
        body = body.translated (0.0f, 1.0f);
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
        g.fillRoundedRectangle (body.translated (0.0f, 2.0f), radius);
    }

    juce::ColourGradient grad (juce::Colour (lit ? 0xFFE9C97F : 0xFF5A3A20), 0.0f, body.getY(),
                               juce::Colour (lit ? 0xFF8E6A2C : 0xFF22130A), 0.0f, body.getBottom(), false);
    grad.addColour (0.5, juce::Colour (lit ? 0xFFC39A47 : 0xFF3A2312));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (body, radius);

    if (shouldDrawButtonAsHighlighted && ! lit)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f * alpha));
        g.fillRoundedRectangle (body, radius);
    }

    // A lit button also glows a little round the edge.
    if (lit)
    {
        g.setColour (glow().withAlpha (0.35f * alpha));
        g.drawRoundedRectangle (body.expanded (0.5f), radius + 0.5f, 2.0f);
    }

    g.setColour ((lit ? litTick() : goldRim()).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (body, radius, 1.2f);

    g.setColour (juce::Colours::white.withAlpha ((lit ? 0.45f : 0.2f) * alpha));
    g.drawLine (body.getX() + 5.0f, body.getY() + 1.8f, body.getRight() - 5.0f, body.getY() + 1.8f, 1.0f);
}

void CeoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool shouldDrawButtonAsDown)
{
    using namespace CeoStyle;

    const bool lit = button.getToggleState() || shouldDrawButtonAsDown;
    auto area = button.getLocalBounds().toFloat().reduced (3.0f, 0.0f).withTrimmedBottom (2.0f);
    if (shouldDrawButtonAsDown)
        area = area.translated (0.0f, 1.0f);

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour ((lit ? background() : buttonText()).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.drawText (button.getButtonText(), area, juce::Justification::centred, true);
}

juce::Font CeoLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    const float size = buttonHeight >= 40 ? 15.0f : 12.5f;             // Capture and Strike are the big ones
    return CeoStyle::font (size).withExtraKerningFactor (0.8f / size);
}

void CeoLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace CeoStyle;

    const float alpha = box.isEnabled() ? 1.0f : 0.4f;
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    // Dark inset with a brass keyline
    g.setColour (juce::Colour (0xFF120A05).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (goldRim().withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r, 4.0f, 1.1f);
    g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
    g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 1.0f);

    // Small gold arrow
    const float cy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle ((float) width - 16.0f, cy - 2.0f, (float) width - 8.0f, cy - 2.0f, (float) width - 12.0f, cy + 3.0f);
    g.setColour (lamp().withMultipliedAlpha (alpha));
    g.fillPath (arrow);
}

void CeoLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font CeoLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return CeoStyle::font (12.5f);
}

juce::Font CeoLookAndFeel::getPopupMenuFont()
{
    return CeoStyle::font (13.0f);
}

juce::Font CeoLookAndFeel::getSliderPopupFont (juce::Slider&)
{
    return CeoStyle::fontMedium (13.0f);
}

void CeoLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height, const juce::PopupMenu::Options&)
{
    using namespace CeoStyle;

    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setGradientFill (juce::ColourGradient (panelTop(), 0.0f, 0.0f, panelBottom(), 0.0f, (float) height, false));
    g.fillRect (r);
    g.setColour (goldRim());
    g.drawRect (r.reduced (0.5f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRect (r.reduced (1.5f), 1.0f);
}

void CeoLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour (CeoStyle::goldRim().withAlpha (isMouseOver || isMouseDragging ? 0.9f : 0.45f));
    for (int i = 1; i <= 3; ++i)
    {
        const float d = (float) i * 4.0f;
        g.drawLine ((float) w - d, (float) h - 1.0f, (float) w - 1.0f, (float) h - d, 1.2f);
    }
}
