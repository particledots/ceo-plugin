// Colours, fonts and small text helpers of the Ceo panel: option C "Sparks" (milestone 13).
// Values come from claude/ceo-look-option-c-spec.md (brass workshop on dark brown, phosphor stream display).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace CeoStyle
{
    // Panel and metal
    inline juce::Colour windowBackground()  { return juce::Colour (0xFF0A0603); }   // behind the rounded panel corners
    inline juce::Colour panelTop()          { return juce::Colour (0xFF2D1B0F); }
    inline juce::Colour panelBottom()       { return juce::Colour (0xFF160B05); }
    inline juce::Colour background()        { return juce::Colour (0xFF1E1109); }   // drop-down menus; text on a lit button
    inline juce::Colour border()            { return juce::Colour (0xFF7A5630); }   // outer edge of the panel
    inline juce::Colour knobFill()          { return juce::Colour (0xFF3A2312); }   // middle of a button
    inline juce::Colour goldRim()           { return juce::Colour (0xFFB8903F); }   // brass keyline
    inline juce::Colour goldPointer()       { return juce::Colour (0xFFFFEDB0); }   // knob pointer
    inline juce::Colour lamp()              { return juce::Colour (0xFFE3BE6A); }   // little lamps, menu arrows
    inline juce::Colour glow()              { return juce::Colour (0xFFFFD98A); }   // lit scale ticks, halos
    inline juce::Colour litTick()           { return juce::Colour (0xFFFFE7A0); }
    inline juce::Colour dimTick()           { return juce::Colour (0xFF7A5A30); }
    inline juce::Colour lettering()         { return juce::Colour (0xFFDCB968); }   // labels
    inline juce::Colour buttonText()        { return juce::Colour (0xFFF0D487); }
    inline juce::Colour menuText()          { return juce::Colour (0xFFE3C777); }
    inline juce::Colour greekDim()          { return juce::Colour (0xFFC9A456); }   // layer letters, not selected
    inline juce::Colour greekSelected()     { return juce::Colour (0xFFFFF1C4); }
    inline juce::Colour displayBackground() { return juce::Colour (0xFF0E0A07); }   // glass
    inline juce::Colour cream()             { return juce::Colour (0xFFE8D9A8); }
    inline juce::Colour amberNeedle()       { return juce::Colour (0xFFFFD58A); }   // Scan marker

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

    // Fonts (built into the plugin, see CeoArt). Sizes are in panel units, like the mockup's SVG font sizes.
    juce::Font font (float size);                 // Jost: labels, buttons, menus, status lines
    juce::Font fontMedium (float size);           // Jost Medium
    juce::Font fontItalic (float size);           // Jost Italic
    juce::Font fontTitle (float size);            // Jost Medium: the title
    juce::Font fontGreek (float size);            // Didact Gothic, Greek letters only

    // Draws one line of text centred in 'area'. Greek letters (the layer names in the status line) are drawn
    // with the Greek font, everything else with 'latin'. If the line is too wide it is cut with "...".
    void drawMixedText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                        const juce::Font& latin, const juce::Font& greek);

    // True when the system asks for less motion (macOS: System Settings > Accessibility > Display > Reduce motion).
    bool prefersReducedMotion();
}

// Implemented per platform in ReduceMotion.cpp.
bool ceoSystemPrefersReducedMotion();
