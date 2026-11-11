// The look of the Ceo panel: knobs, buttons and drop-downs, option C "Sparks" (milestone 13).
// Colours and fonts are in CeoStyle.h; the cached pictures (knob body, scale ticks, ...) are in CeoArt.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CeoStyle.h"
#include "CeoArt.h"

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
    juce::Font getSliderPopupFont (juce::Slider&) override;
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height, const juce::PopupMenu::Options&) override;

    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

private:
    juce::SharedResourcePointer<CeoArt> art;      // the fonts and cached pictures; lives as long as an editor does
};
