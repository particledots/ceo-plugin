// The plugin wrapper. It talks to Logic, reads the control values and passes
// them, with the audio, to the engine. It contains no sound-making code.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <ceo/Engine.h>
#include "Parameters.h"

class CeoAudioProcessor : public juce::AudioProcessor,
                          private juce::AsyncUpdater
{
public:
    CeoAudioProcessor();
    ~CeoAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }  // grains, echoes and reverb can ring on after the input stops

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // For the panel: the controls, and read-only information from the engine (stream display).
    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    const ceo::Engine& getEngine() const { return engine; }

private:
    // Called on the main thread after a Capture recording filled its layer: switches the Capture button off.
    void handleAsyncUpdate() override;

    ceo::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    ceo::Engine engine;
    uint32_t seenCaptureRanOut = 0;     // audio thread only: how many 'layer full' events were already passed on

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeoAudioProcessor)
};
