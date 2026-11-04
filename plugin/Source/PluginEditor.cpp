#include "PluginEditor.h"

namespace
{
    constexpr int kKnobSize   = 60;
    constexpr int kCellWidth  = 140;
    constexpr int kLabelGap   = 62;     // from the top of a knob to its label
    constexpr int kLabelHeight = 16;
    constexpr int kExtraTop   = 82;     // from the top of a knob to the small control under its label
}

//==============================================================================
// Layout (all in the fixed 500 x 894 space)
//   title                                         y 12
//   row 0: Input level / Dry/wet / Output level    y 52
//   Capture (+ Onset) / layer dial / Strike (+ Erase, Undo)   y 144 to 294
//   stream display                                y 304 to 400
//   direction lights above Grain direction        y 413
//   rows 1..4 of three knobs, 118 apart           from y 424
//   Follow under Scan, Mod target under Mod amount, Input mode under Sense
//==============================================================================

int CeoPanel::rowTop (int row)
{
    return row == 0 ? 52 : 424 + (row - 1) * 118;
}

CeoPanel::CeoPanel (CeoAudioProcessor& p)
    : processor (p),
      state (p.getValueTreeState()),
      layerDial (*p.getValueTreeState().getParameter (CeoParams::layer)),
      display (p.getEngine(), p.getValueTreeState())
{
    // The 15 knobs, in panel order.
    addKnob (CeoParams::inputLevel,     "Input level",     0, 0);
    addKnob (CeoParams::dryWet,         "Dry/wet",         0, 1);
    addKnob (CeoParams::outputLevel,    "Output level",    0, 2);

    addKnob (CeoParams::intensity,      "Intensity",       1, 0);
    addKnob (CeoParams::grainWindow,    "Grain window",    1, 1);
    addKnob (CeoParams::grainDirection, "Grain direction", 1, 2);

    addKnob (CeoParams::length,         "Length",          2, 0);
    addKnob (CeoParams::scan,           "Scan",            2, 1);
    addKnob (CeoParams::spray,          "Spray",           2, 2);

    addKnob (CeoParams::pitch,          "Pitch",           3, 0);
    addKnob (CeoParams::pitchDeviation, "Pitch deviation", 3, 1);
    addKnob (CeoParams::modAmount,      "Mod amount",      3, 2);

    addKnob (CeoParams::hold,           "Hold",            4, 0);
    addKnob (CeoParams::sense,          "Sense",           4, 1);
    addKnob (CeoParams::dub,            "Dub",             4, 2);

    // Buttons. Capture, Onset and Follow stay on or off. Strike, Erase and Undo act on every click.
    setUpLatchingButton (captureButton, "Capture", CeoParams::capture,      captureAttachment);
    setUpLatchingButton (onsetButton,   "Onset",   CeoParams::onsetCapture, onsetAttachment);
    setUpLatchingButton (followButton,  "Follow",  CeoParams::follow,       followAttachment);
    setUpClickButton    (strikeButton,  "Strike",  CeoParams::strike);
    setUpClickButton    (eraseButton,   "Erase",   CeoParams::erase);
    setUpClickButton    (undoButton,    "Undo",    CeoParams::undo);

    setUpDropdown (inputModeBox, CeoParams::inputMode, inputModeAttachment);
    setUpDropdown (modTargetBox, CeoParams::modTarget, modTargetAttachment);

    addAndMakeVisible (layerDial);
    addAndMakeVisible (display);

    setSize (kWidth, kHeight);
    startTimerHz (30);
}

CeoPanel::~CeoPanel()
{
    stopTimer();
}

void CeoPanel::addKnob (const char* parameterId, const char* label, int row, int column)
{
    auto cell = std::make_unique<KnobCell>();
    cell->label  = label;
    cell->row    = row;
    cell->column = column;

    auto* parameter = state.getParameter (parameterId);
    const double defaultValue = parameter != nullptr ? (double) parameter->getDefaultValue() : 0.5;

    cell->attachment = std::make_unique<SliderAttachment> (state, parameterId, cell->knob);
    cell->knob.setDoubleClickReturnValue (true, defaultValue);
    cell->knob.setCentreDetent (std::abs (defaultValue - 0.5) < 1.0e-6);
    cell->knob.setPopupDisplayEnabled (true, false, this);
    addAndMakeVisible (cell->knob);

    knobs.push_back (std::move (cell));
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
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    attachment = std::make_unique<ButtonAttachment> (state, parameterId, button);
    addAndMakeVisible (button);
}

// Strike, Erase and Undo count every change of their switch as one click (the engine works that way), so each
// click of the button flips the switch once. The button only looks pressed while the mouse is held down.
void CeoPanel::setUpClickButton (juce::TextButton& button, const char* text, const char* parameterId)
{
    button.setButtonText (text);
    button.setClickingTogglesState (false);
    button.onClick = [this, parameterId] { flipParameter (parameterId); };
    addAndMakeVisible (button);
}

void CeoPanel::setUpDropdown (juce::ComboBox& box, const char* parameterId, std::unique_ptr<ComboBoxAttachment>& attachment)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId)))
        box.addItemList (choice->choices, 1);

    attachment = std::make_unique<ComboBoxAttachment> (state, parameterId, box);
    addAndMakeVisible (box);
}

juce::Rectangle<int> CeoPanel::knobBounds (const KnobCell& cell) const
{
    return { columnCentre (cell.column) - kKnobSize / 2, rowTop (cell.row), kKnobSize, kKnobSize };
}

juce::Rectangle<int> CeoPanel::ledArea() const
{
    const int cx = columnCentre (2);
    return { cx - 40, 403, 80, 20 };
}

void CeoPanel::resized()
{
    for (auto& cell : knobs)
        cell->knob.setBounds (knobBounds (*cell));

    const int dialY = 144;
    layerDial.setBounds (250 - LayerDial::kWidth / 2, dialY, LayerDial::kWidth, LayerDial::kHeight);

    // Capture (and Onset under it) on the left, Strike (and Erase, Undo under it) on the right,
    // lined up with the middle of the layer dial.
    const int midY = dialY + 74;
    captureButton.setBounds (52,  midY - 17, 96, 34);
    onsetButton.setBounds   (52,  midY + 25, 96, 24);
    strikeButton.setBounds  (352, midY - 17, 96, 34);
    eraseButton.setBounds   (352, midY + 25, 46, 24);
    undoButton.setBounds    (402, midY + 25, 46, 24);

    display.setBounds (30, 304, 440, 96);

    auto extraUnder = [this] (int row, int column, int width, int height)
    {
        return juce::Rectangle<int> (width, height).withCentre ({ columnCentre (column), rowTop (row) + kExtraTop + height / 2 });
    };
    followButton.setBounds (extraUnder (2, 1, 76, 24));
    modTargetBox.setBounds (extraUnder (3, 2, 92, 24));
    inputModeBox.setBounds (extraUnder (4, 1, 92, 24));
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
                    juce::Rectangle<int> (columnCentre (cell->column) - kCellWidth / 2, rowTop (cell->row) + kLabelGap, kCellWidth, kLabelHeight),
                    juce::Justification::centred, false);

    // The two direction lights above Grain direction: backwards on the left, forwards on the right.
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
