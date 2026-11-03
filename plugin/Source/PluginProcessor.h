// The plugin wrapper. It talks to Logic, reads the control values and passes
// them, with the audio, to the engine. It contains no sound-making code.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <ceo/Engine.h>
#include "Parameters.h"

class CeoAudioProcessor : public juce::AudioProcessor
{
public:
    CeoAudioProcessor();
    ~CeoAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 3.0; }   // grains can ring on for up to 3 s after the input stops

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    ceo::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    ceo::Engine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeoAudioProcessor)
};
