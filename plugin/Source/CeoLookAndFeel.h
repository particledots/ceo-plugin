// The look of the Ceo panel: colours and the drawing of knobs, buttons and dropdowns.
// Colours and shapes follow the approved mockup (ceo-panel-mockup.html) and ceo-design-spec.md.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace CeoStyle
{
    inline juce::Colour background()        { return juce::Colour (0xFF2B1A10); }   // dark brown panel
    inline juce::Colour border()            { return juce::Colour (0xFF5A3A22); }
    inline juce::Colour knobFill()          { return juce::Colour (0xFF3A2616); }
    inline juce::Colour goldRim()           { return juce::Colour (0xFFC9A24B); }
    inline juce::Colour goldPointer()       { return juce::Colour (0xFFE3BE6A); }
    inline juce::Colour lettering()         { return juce::Colour (0xFFD4AF5F); }
    inline juce::Colour title()             { return juce::Colour (0xFFE3BE6A); }
    inline juce::Colour displayBackground() { return juce::Colour (0xFF14110F); }
    inline juce::Colour cream()             { return juce::Colour (0xFFE8D9A8); }
    inline juce::Colour selectedRing()      { return juce::Colour (0xFFF5E6B8); }

    // Layer colours: alpha, beta, gamma, delta, epsilon, zeta, then Omega (index 6).
    inline juce::Colour layerColour (int index)
    {
        static const juce::uint32 argb[7] = { 0xFF5BA3E8, 0xFFE0679A, 0xFFD98A2B, 0xFFF2D04B, 0xFF3FC49A, 0xFF9A93F0, 0xFFE8E2D0 };
        return juce::Colour (argb[juce::jlimit (0, 6, index)]);
    }

    // The Greek letters on the layer dial (UTF-8 so they survive any compiler setting).
    inline juce::String layerName (int index)
    {
        static const char* const utf8[7] = { "\xce\xb1", "\xce\xb2", "\xce\xb3", "\xce\xb4", "\xce\xb5", "\xce\xb6", "\xce\xa9" };
        return juce::String::fromUTF8 (utf8[juce::jlimit (0, 6, index)]);
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

class CeoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CeoLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};
