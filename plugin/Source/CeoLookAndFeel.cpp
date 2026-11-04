#include "CeoLookAndFeel.h"

CeoLookAndFeel::CeoLookAndFeel()
{
    using namespace CeoStyle;

    setColour (juce::ComboBox::backgroundColourId,         knobFill());
    setColour (juce::ComboBox::textColourId,               lettering());
    setColour (juce::ComboBox::outlineColourId,            goldRim());
    setColour (juce::ComboBox::arrowColourId,              lettering());
    setColour (juce::ComboBox::focusedOutlineColourId,     goldPointer());

    setColour (juce::PopupMenu::backgroundColourId,            background());
    setColour (juce::PopupMenu::textColourId,                  lettering());
    setColour (juce::PopupMenu::headerTextColourId,            lettering());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, goldRim());
    setColour (juce::PopupMenu::highlightedTextColourId,       background());

    // The value bubble shown while a knob is dragged.
    setColour (juce::BubbleComponent::backgroundColourId,  displayBackground());
    setColour (juce::BubbleComponent::outlineColourId,     goldRim());
    setColour (juce::TooltipWindow::textColourId,          lettering());
}

void CeoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    using namespace CeoStyle;

    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();

    g.setColour (knobFill());
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    const bool active = slider.isMouseOverOrDragging();
    g.setColour (active ? goldPointer() : goldRim());
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, active ? 1.8f : 1.3f);

    // Pointer: from near the middle out towards the rim, angle measured from 12 o'clock, clockwise.
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float s = std::sin (angle), c = -std::cos (angle);
    juce::Path pointer;
    pointer.startNewSubPath (centre.x + s * radius * 0.20f, centre.y + c * radius * 0.20f);
    pointer.lineTo          (centre.x + s * radius * 0.80f, centre.y + c * radius * 0.80f);
    g.setColour (goldPointer());
    g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CeoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    using namespace CeoStyle;

    const bool lit = button.getToggleState() || shouldDrawButtonAsDown;
    const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = juce::jmin (7.0f, r.getHeight() * 0.35f);

    auto fill = lit ? goldRim() : knobFill();
    if (shouldDrawButtonAsHighlighted && ! lit)
        fill = fill.brighter (0.15f);

    const float alpha = button.isEnabled() ? 1.0f : 0.4f;      // greyed out when it cannot be used now
    g.setColour (fill.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, corner);
    g.setColour (goldRim().withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r, corner, 1.2f);
}

void CeoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool shouldDrawButtonAsDown)
{
    using namespace CeoStyle;

    const bool lit = button.getToggleState() || shouldDrawButtonAsDown;
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour ((lit ? background() : lettering()).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (3, 0), juce::Justification::centred, true);
}

juce::Font CeoLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return CeoStyle::font (juce::jmin (15.0f, (float) buttonHeight * 0.5f));
}

void CeoLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace CeoStyle;

    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (knobFill());
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.isEnabled() ? goldRim() : goldRim().withAlpha (0.4f));
    g.drawRoundedRectangle (r, 5.0f, 1.2f);

    const float cx = (float) width - 11.0f, cy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 2.5f);
    g.setColour (lettering());
    g.fillPath (arrow);
}

void CeoLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (7, 1, box.getWidth() - 24, box.getHeight() - 2);
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

void CeoLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour (CeoStyle::goldRim().withAlpha (isMouseOver || isMouseDragging ? 0.9f : 0.45f));
    for (int i = 1; i <= 3; ++i)
    {
        const float d = (float) i * 4.0f;
        g.drawLine ((float) w - d, (float) h - 1.0f, (float) w - 1.0f, (float) h - d, 1.2f);
    }
}
