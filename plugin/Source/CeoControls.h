// The custom controls of the Ceo panel: the knob and the layer dial.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// A rotary knob. Drag up/down (or turn the mouse wheel); double-click goes back to the default.
// Knobs whose default is the centre click into the centre position when dragged across it.
class CeoKnob : public juce::Slider
{
public:
    CeoKnob();

    void setCentreDetent (bool shouldSnap) { centreDetent = shouldSnap; }

    double snapValue (double attemptedValue, DragMode) override;

private:
    bool centreDetent = false;
};

// The layer dial: a round dial with seven indicators around it (alpha to zeta, then Omega).
// Click an indicator, drag around the dial, or use the mouse wheel. It is attached to the "layer" choice parameter.
class LayerDial : public juce::Component
{
public:
    explicit LayerDial (juce::RangedAudioParameter& layerParameter);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kWidth  = 150;
    static constexpr int kHeight = 160;

private:
    juce::Point<float> dialCentre() const { return { (float) getWidth() * 0.5f, 74.0f }; }
    void pickFromMouse (const juce::MouseEvent&);
    void step (int direction);

    juce::ParameterAttachment attachment;
    int current = 0;                     // 0..5 = alpha..zeta, 6 = Omega
    float wheelAccumulator = 0.0f;
};
