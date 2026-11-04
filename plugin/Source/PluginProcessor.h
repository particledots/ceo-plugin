// The plugin wrapper. It talks to Logic, reads the control values and passes
// them, with the audio, to the engine. It contains no sound-making code.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <ceo/Engine.h>
#include <ceo/LayerLibrary.h>
#include "Parameters.h"

class CeoAudioProcessor : public juce::AudioProcessor,
                          private juce::AsyncUpdater,
                          private juce::Timer
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

    // Milestone 10: layers saved to and loaded from WAV files. Main thread only.
    ceo::LayerLibrary& getLayerLibrary() { return library; }
    juce::File getHomeFolder() const { return homeFolder; }
    void setHomeFolder (const juce::File&);

private:
    // Called on the main thread after a Capture recording filled its layer: switches the Capture button off.
    void handleAsyncUpdate() override;
    void timerCallback() override;          // 10 times a second: lets the file library move its work along

    ceo::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    ceo::Engine engine;
    ceo::LayerLibrary library { engine };   // after the engine: it is destroyed first
    juce::File homeFolder;
    uint32_t seenCaptureRanOut = 0;     // audio thread only: how many 'layer full' events were already passed on

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeoAudioProcessor)
};
