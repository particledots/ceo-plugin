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

// Everything on the panel, laid out in a fixed 784 x 672 space (milestone 7: squarer layout). The editor scales it to the window size.
class CeoPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth  = 784;
    static constexpr int kHeight = 672;

    explicit CeoPanel (CeoAudioProcessor&);
    ~CeoPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The ids of all parameters that have a control on the panel (used by the local check).
    const std::vector<juce::String>& controlledParameterIds() const { return controlIds; }

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct KnobCell
    {
        const char* label = "";
        int centreX = 0;                  // middle of the knob across the panel
        int row = 0;                      // 0..3, top to bottom, in its block
        CeoKnob knob;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    CeoKnob& addKnob (const char* parameterId, const char* label, int centreX, int row);
    void setUpPitchReadout (CeoKnob&);
    void setUpLatchingButton (juce::TextButton&, const char* text, const char* parameterId, std::unique_ptr<ButtonAttachment>&);
    void setUpClickButton (juce::TextButton&, const char* text, const char* parameterId);
    void setUpDropdown (juce::ComboBox&, const char* parameterId, std::unique_ptr<ComboBoxAttachment>&);
    void flipParameter (const char* parameterId);

    // Milestone 10: SAVE WAV / LOAD WAV / FOLDER... and the two status lines under Capture and Strike.
    void updateFileStrip();
    void startSave (int layer);
    void startLoad (int layer);
    void chooseHome();
    void showMessage (const juce::String&);
    int  selectedLayer() const;

    // The two knob blocks (left: record and output, right: the grains), two columns each.
    static constexpr int kLeftColumn0  = 70,  kLeftColumn1  = 170;
    static constexpr int kRightColumn0 = kWidth - 170, kRightColumn1 = kWidth - 70;
    static constexpr int kLeftBlockMiddle = 120;           // Output level sits alone, in the middle of the left block

    static int rowTop (int row);
    juce::Rectangle<int> knobBounds (const KnobCell&) const;
    juce::Rectangle<int> ledArea() const;

    // The two menus of milestone 8, in the places kept free for them: Pitch Snap under Pitch, Pitch Scale under
    // Pitch deviation.
    static juce::Rectangle<int> pitchSnapSlot()  { return juce::Rectangle<int> (84, 24).withCentre  ({ kRightColumn0, rowTop (3) + 82 + 12 }); }
    static juce::Rectangle<int> pitchScaleSlot() { return juce::Rectangle<int> (108, 24).withCentre ({ kRightColumn1, rowTop (3) + 82 + 12 }); }

    CeoAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::vector<juce::String> controlIds;      // every parameter that has a control on the panel

    // Components (declared before the attachments, so the attachments are destroyed first).
    std::vector<std::unique_ptr<KnobCell>> knobs;
    LayerDial layerDial;
    StreamDisplay display;
    juce::TextButton captureButton, onsetButton, strikeButton, eraseButton, undoButton, followButton;
    juce::ComboBox inputModeBox, modTargetBox, pitchSnapBox, pitchScaleBox;
    juce::TextButton saveWavButton, loadWavButton, folderButton;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String statusA, statusB;
    juce::Colour statusColour;
    juce::String messageText;
    int messageTicks = 0;

    std::unique_ptr<ButtonAttachment> captureAttachment, onsetAttachment, followAttachment;
    std::unique_ptr<ComboBoxAttachment> inputModeAttachment, modTargetAttachment, pitchSnapAttachment, pitchScaleAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeoPanel)
};

class CeoEditor : public juce::AudioProcessorEditor
{
public:
    explicit CeoEditor (CeoAudioProcessor&);
    ~CeoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    const std::vector<juce::String>& controlledParameterIds() const { return panel.controlledParameterIds(); }

private:
    CeoLookAndFeel lookAndFeel;      // declared before the panel, so it outlives it
    CeoPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeoEditor)
};
