// The custom controls of the Ceo panel: the knob and the layer dial.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CeoArt.h"

// A rotary knob. Drag up/down (or turn the mouse wheel); double-click goes back to the default.
// Knobs whose default is the centre click into the centre position when dragged across it.
class CeoKnob : public juce::Slider
{
public:
    CeoKnob();

    void setCentreDetent (bool shouldSnap) { centreDetent = shouldSnap; }

    double snapValue (double attemptedValue, DragMode) override;

    // The knob is drawn in an 84 x 84 square (its scale ticks reach beyond the old 60 x 60), so only
    // the round part of it takes the mouse: neighbours, labels and menus keep their clicks.
    bool hitTest (int x, int y) override
    {
        const float dx = (float) x - (float) getWidth() * 0.5f, dy = (float) y - (float) getHeight() * 0.5f;
        return dx * dx + dy * dy <= 41.0f * 41.0f;
    }

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

    // Everything is drawn at kScale times the size of the original portrait panel's dial.
    static constexpr float kScale  = 2.0f;
    static constexpr int   kWidth  = CeoGeo::dialW;
    static constexpr int   kHeight = CeoGeo::dialH;

    // Called by the panel's timer: the halo of the selected layer breathes (1.8 s). Nothing moves
    // when the system asks for reduced motion.
    void tickAnimation();
    void setReducedMotion (bool shouldReduce);

private:
    juce::Point<float> dialCentre() const { return { (float) getWidth() * 0.5f, CeoGeo::dialCy }; }
    void pickFromMouse (const juce::MouseEvent&);
    void step (int direction);

    juce::ParameterAttachment attachment;
    int current = 0;                     // 0..5 = alpha..zeta, 6 = Omega
    float wheelAccumulator = 0.0f;
    bool reducedMotion = false;
    juce::SharedResourcePointer<CeoArt> art;
};
