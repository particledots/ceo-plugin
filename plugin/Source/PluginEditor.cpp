#include "PluginEditor.h"
#include <ceo/Mapping.h>

namespace
{
    constexpr int kKnobSize   = 60;
    constexpr int kCellWidth  = 110;
    constexpr int kLabelGap   = 62;     // from the top of a knob to its label
    constexpr int kLabelHeight = 16;
    constexpr int kExtraTop   = 82;     // from the top of a knob to the small control under its label
    constexpr int kRowPitch   = 116;    // from one knob row to the next
}

//==============================================================================
// Layout (all in the fixed 784 x 672 space)
//   title                                           y 10 to 42
//   left block (x 20 to 220): Input level / Sense, Hold / Dub, Mod amount / Dry/wet, Output level (alone)
//   right block (x 564 to 764): Intensity / Grain window, Grain direction / Length, Scan / Spray, Pitch / Pitch deviation
//   rows 116 apart from y 52; the small controls sit under the label of their knob (y +82)
//   Pitch Snap menu under Pitch, Pitch Scale menu under Pitch deviation (milestone 8)
//   layer dial, centre at (392, 212), 2 times the old size
//   Capture (+ Onset under it) left and Strike (+ Erase, Undo under it) right, under the dial, y 372 to 452
//   stream display x 112 to 672, y 526 to 652
//==============================================================================

int CeoPanel::rowTop (int row)
{
    return 52 + kRowPitch * row;
}

CeoPanel::CeoPanel (CeoAudioProcessor& p)
    : processor (p),
      state (p.getValueTreeState()),
      layerDial (*p.getValueTreeState().getParameter (CeoParams::layer)),
      display (p.getEngine(), p.getValueTreeState())
{
    // The 15 knobs. Left block: record and output.
    addKnob (CeoParams::inputLevel,     "Input level",     kLeftColumn0,     0);
    addKnob (CeoParams::sense,          "Sense",           kLeftColumn1,     0);
    addKnob (CeoParams::hold,           "Hold",            kLeftColumn0,     1);
    addKnob (CeoParams::dub,            "Dub",             kLeftColumn1,     1);
    addKnob (CeoParams::modAmount,      "Mod amount",      kLeftColumn0,     2);
    addKnob (CeoParams::dryWet,         "Dry/wet",         kLeftColumn1,     2);
    addKnob (CeoParams::outputLevel,    "Output level",    kLeftBlockMiddle, 3);

    // Right block: the grains.
    addKnob (CeoParams::intensity,      "Intensity",       kRightColumn0,    0);
    addKnob (CeoParams::grainWindow,    "Grain window",    kRightColumn1,    0);
    addKnob (CeoParams::grainDirection, "Grain direction", kRightColumn0,    1);
    addKnob (CeoParams::length,         "Length",          kRightColumn1,    1);
    addKnob (CeoParams::scan,           "Scan",            kRightColumn0,    2);
    addKnob (CeoParams::spray,          "Spray",           kRightColumn1,    2);
    setUpPitchReadout (addKnob (CeoParams::pitch, "Pitch", kRightColumn0, 3));
    addKnob (CeoParams::pitchDeviation, "Pitch deviation", kRightColumn1,    3);

    // Buttons. Capture, Onset and Follow stay on or off. Strike, Erase and Undo act on every click.
    setUpLatchingButton (captureButton, "Capture", CeoParams::capture,      captureAttachment);
    setUpLatchingButton (onsetButton,   "Onset",   CeoParams::onsetCapture, onsetAttachment);
    setUpLatchingButton (followButton,  "Follow",  CeoParams::follow,       followAttachment);
    setUpClickButton    (strikeButton,  "Strike",  CeoParams::strike);
    setUpClickButton    (eraseButton,   "Erase",   CeoParams::erase);
    setUpClickButton    (undoButton,    "Undo",    CeoParams::undo);

    setUpDropdown (inputModeBox, CeoParams::inputMode, inputModeAttachment);
    setUpDropdown (modTargetBox, CeoParams::modTarget, modTargetAttachment);
    setUpDropdown (pitchSnapBox,  CeoParams::pitchSnap,  pitchSnapAttachment);     // under Pitch
    setUpDropdown (pitchScaleBox, CeoParams::pitchScale, pitchScaleAttachment);    // under Pitch deviation

    addAndMakeVisible (layerDial);
    addAndMakeVisible (display);
    controlIds.push_back (CeoParams::layer);          // the dial is attached to the layer parameter

    setSize (kWidth, kHeight);
    startTimerHz (30);
}

CeoPanel::~CeoPanel()
{
    stopTimer();
}

CeoKnob& CeoPanel::addKnob (const char* parameterId, const char* label, int centreX, int row)
{
    auto cell = std::make_unique<KnobCell>();
    cell->label   = label;
    cell->centreX = centreX;
    cell->row     = row;
    controlIds.push_back (parameterId);

    auto* parameter = state.getParameter (parameterId);
    const double defaultValue = parameter != nullptr ? (double) parameter->getDefaultValue() : 0.5;

    cell->attachment = std::make_unique<SliderAttachment> (state, parameterId, cell->knob);
    cell->knob.setDoubleClickReturnValue (true, defaultValue);
    cell->knob.setCentreDetent (std::abs (defaultValue - 0.5) < 1.0e-6);
    cell->knob.setPopupDisplayEnabled (true, false, this);
    addAndMakeVisible (cell->knob);

    CeoKnob& knob = cell->knob;
    knobs.push_back (std::move (cell));
    return knob;
}

// The Pitch knob's value bubble (shown while it is dragged) gives the note that really plays, in semitones, as
// the engine works it out: with Pitch Snap on a whole number such as -12 or +7, with Snap Off the smooth value
// to two decimals. It uses the same maths as the engine (ceo/Mapping.h), so the two cannot disagree.
void CeoPanel::setUpPitchReadout (CeoKnob& knob)
{
    knob.textFromValueFunction = [this] (double knobPosition)
    {
        const int mode = (int) (state.getRawParameterValue (CeoParams::pitchSnap)->load() + 0.5f);
        const float semitones = ceo::map::pitchSemitonesSnapped ((float) knobPosition, mode);

        if (mode == ceo::map::kSnapModeOff)
        {
            const double rounded = std::round ((double) semitones * 100.0) / 100.0;
            if (rounded == 0.0)
                return juce::String ("0.00");
            return (rounded > 0.0 ? juce::String ("+") : juce::String()) + juce::String (rounded, 2);
        }

        const int whole = (int) std::lround (semitones);
        return whole > 0 ? "+" + juce::String (whole) : juce::String (whole);
    };
}

void CeoPanel::flipParameter (const char* parameterId)
{
    if (auto* parameter = state.getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);
        parameter->endChangeGesture();
    }
}

void CeoPanel::setUpLatchingButton (juce::TextButton& button, const char* text, const char* parameterId,
                                    std::unique_ptr<ButtonAttachment>& attachment)
{
    controlIds.push_back (parameterId);
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    attachment = std::make_unique<ButtonAttachment> (state, parameterId, button);
    addAndMakeVisible (button);
}

// Strike, Erase and Undo count every change of their switch as one click (the engine works that way), so each
// click of the button flips the switch once. The button only looks pressed while the mouse is held down.
void CeoPanel::setUpClickButton (juce::TextButton& button, const char* text, const char* parameterId)
{
    controlIds.push_back (parameterId);
    button.setButtonText (text);
    button.setClickingTogglesState (false);
    button.onClick = [this, parameterId] { flipParameter (parameterId); };
    addAndMakeVisible (button);
}

void CeoPanel::setUpDropdown (juce::ComboBox& box, const char* parameterId, std::unique_ptr<ComboBoxAttachment>& attachment)
{
    controlIds.push_back (parameterId);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId)))
        box.addItemList (choice->choices, 1);

    attachment = std::make_unique<ComboBoxAttachment> (state, parameterId, box);
    addAndMakeVisible (box);
}

juce::Rectangle<int> CeoPanel::knobBounds (const KnobCell& cell) const
{
    return { cell.centreX - kKnobSize / 2, rowTop (cell.row), kKnobSize, kKnobSize };
}

juce::Rectangle<int> CeoPanel::ledArea() const
{
    const int cx = kRightColumn0;
    return { cx - 40, rowTop (1) + kExtraTop, 80, 20 };       // under the Grain direction label, clear of the value bubble
}

void CeoPanel::resized()
{
    for (auto& cell : knobs)
        cell->knob.setBounds (knobBounds (*cell));

    // The layer dial, centred; its middle is at (392, 212).
    layerDial.setBounds (kWidth / 2 - LayerDial::kWidth / 2, 72, LayerDial::kWidth, LayerDial::kHeight);

    // Under the dial: Capture (and Onset under it) on the left, Strike (and Erase, Undo under it) on the right.
    captureButton.setBounds (240, 372, 100, 44);
    onsetButton.setBounds   (240, 424, 100, 28);
    strikeButton.setBounds  (444, 372, 100, 44);
    eraseButton.setBounds   (444, 424,  48, 28);
    undoButton.setBounds    (496, 424,  48, 28);

    display.setBounds (112, 526, 560, 126);

    auto extraUnder = [this] (int row, int centreX, int width, int height)
    {
        return juce::Rectangle<int> (width, height).withCentre ({ centreX, rowTop (row) + kExtraTop + height / 2 });
    };
    followButton.setBounds (extraUnder (2, kRightColumn0, 76, 24));      // under Scan
    modTargetBox.setBounds (extraUnder (2, kLeftColumn0, 92, 24));       // under Mod amount
    inputModeBox.setBounds (extraUnder (0, kLeftColumn1, 92, 24));       // under Sense
    pitchSnapBox.setBounds  (pitchSnapSlot());                           // under Pitch
    pitchScaleBox.setBounds (pitchScaleSlot());                          // under Pitch deviation
}

void CeoPanel::paint (juce::Graphics& g)
{
    using namespace CeoStyle;

    const auto bounds = getLocalBounds().toFloat();
    g.setColour (background());
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (border());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    // Title "Ceò"
    g.setColour (title());
    g.setFont (font (24.0f, true));
    g.drawText (juce::String::fromUTF8 ("Ce\xc3\xb2"), juce::Rectangle<int> (0, 10, kWidth, 32), juce::Justification::centred, false);

    // Knob labels
    g.setColour (lettering());
    g.setFont (font (12.5f));
    for (const auto& cell : knobs)
        g.drawText (cell->label,
                    juce::Rectangle<int> (cell->centreX - kCellWidth / 2, rowTop (cell->row) + kLabelGap, kCellWidth, kLabelHeight),
                    juce::Justification::centred, false);

    // The two direction lights under Grain direction: backwards on the left, forwards on the right.
    const auto leds = ledArea();
    const float forward = display.forwardActivity();
    const float reverse = display.reverseActivity();
    const float cy = (float) leds.getCentreY();
    const float cx = (float) leds.getCentreX();

    auto drawLed = [&] (float x, float level)
    {
        g.setColour (goldRim().withAlpha (0.55f));
        g.drawEllipse (x - 3.5f, cy - 3.5f, 7.0f, 7.0f, 1.0f);
        g.setColour (goldPointer().withAlpha (0.12f + 0.88f * level));
        g.fillEllipse (x - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    };
    drawLed (cx - 14.0f, reverse);
    drawLed (cx + 14.0f, forward);

    g.setColour (lettering().withAlpha (0.8f));
    juce::Path back, fwd;
    // The arrows point outwards: left = backwards, right = forwards.
    back.addTriangle (cx - 26.0f, cy, cx - 21.0f, cy - 3.0f, cx - 21.0f, cy + 3.0f);
    fwd.addTriangle  (cx + 26.0f, cy, cx + 21.0f, cy - 3.0f, cx + 21.0f, cy + 3.0f);
    g.fillPath (back);
    g.fillPath (fwd);
}

void CeoPanel::timerCallback()
{
    display.tick();
    repaint (ledArea());
}

//==============================================================================
CeoEditor::CeoEditor (CeoAudioProcessor& p)
    : juce::AudioProcessorEditor (p),
      panel (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits ((int) (CeoPanel::kWidth * 0.6), (int) (CeoPanel::kHeight * 0.6),
                     (int) (CeoPanel::kWidth * 1.8), (int) (CeoPanel::kHeight * 1.8));
    if (auto* aspect = getConstrainer())
        aspect->setFixedAspectRatio ((double) CeoPanel::kWidth / (double) CeoPanel::kHeight);

    setSize (CeoPanel::kWidth, CeoPanel::kHeight);
}

CeoEditor::~CeoEditor()
{
    setLookAndFeel (nullptr);
}

void CeoEditor::paint (juce::Graphics& g)
{
    g.fillAll (CeoStyle::background());
}

void CeoEditor::resized()
{
    panel.setBounds (0, 0, CeoPanel::kWidth, CeoPanel::kHeight);
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) CeoPanel::kWidth));
}
