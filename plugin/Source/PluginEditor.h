// The styled panel (milestone 6). It replaces the generic sliders. Every control is attached to the
// same parameter id as before, so saved projects and automation keep working.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CeoLookAndFeel.h"
#include "CeoControls.h"
#include "StreamDisplay.h"

#include <memory>
#include <vector>

// Everything on the panel, laid out in a fixed 500 x 894 space. The editor scales it to the window size.
class CeoPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth  = 500;
    static constexpr int kHeight = 894;

    explicit CeoPanel (CeoAudioProcessor&);
    ~CeoPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct KnobCell
    {
        const char* label = "";
        int row = 0, column = 0;          // row 0 = top row, rows 1..4 = grain rows; columns 0..2
        CeoKnob knob;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (const char* parameterId, const char* label, int row, int column);
    void setUpLatchingButton (juce::TextButton&, const char* text, const char* parameterId, std::unique_ptr<ButtonAttachment>&);
    void setUpClickButton (juce::TextButton&, const char* text, const char* parameterId);
    void setUpDropdown (juce::ComboBox&, const char* parameterId, std::unique_ptr<ComboBoxAttachment>&);
    void flipParameter (const char* parameterId);

    static int rowTop (int row);
    static int columnCentre (int column) { return 100 + 150 * column; }
    juce::Rectangle<int> knobBounds (const KnobCell&) const;
    juce::Rectangle<int> ledArea() const;

    CeoAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    // Components (declared before the attachments, so the attachments are destroyed first).
    std::vector<std::unique_ptr<KnobCell>> knobs;
    LayerDial layerDial;
    StreamDisplay display;
    juce::TextButton captureButton, onsetButton, strikeButton, eraseButton, undoButton, followButton;
    juce::ComboBox inputModeBox, modTargetBox;

    std::unique_ptr<ButtonAttachment> captureAttachment, onsetAttachment, followAttachment;
    std::unique_ptr<ComboBoxAttachment> inputModeAttachment, modTargetAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeoPanel)
};

class CeoEditor : public juce::AudioProcessorEditor
{
public:
    explicit CeoEditor (CeoAudioProcessor&);
    ~CeoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CeoLookAndFeel lookAndFeel;      // declared before the panel, so it outlives it
    CeoPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeoEditor)
};
