// The plugin wrapper. It talks to Logic, reads the control values and passes
// them, with the audio, to the engine. It contains no sound-making code.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <ceo/Engine.h>
#include <ceo/LayerLibrary.h>
#include <ceo/Scene.h>
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

    // Milestone 11: Scenes (a folder of six WAV files plus a settings file). Main thread only.
    struct ScenePlan
    {
        bool ok = false;                       // something will be loaded
        juce::String message;                  // when not ok: why
        juce::File folder;
        ceo::scene::SceneFiles files;
        ceo::scene::Preset preset;
        ceo::scene::Decision decision;
        ceo::scene::LoadWhat what = ceo::scene::AsFileSays;
        juce::StringArray unsavedLayers;       // layers that would be replaced although no file has their audio
    };
    struct SceneResult { bool ok = false; juce::String message; };
    struct SceneStatus { juce::String name; int state = 0; };          // state: 0 none, 1 fine, 2 changed, 3 missing

    ScenePlan planSceneLoad (const juce::File& folder);                // reads the folder, applies the Load setting
    SceneResult runSceneLoad (const ScenePlan&);
    bool startSceneSave (const juce::File& folder, juce::String& whyNot);
    juce::String takeSceneMessage();                                   // a message the panel should show (a finished Scene save), or empty
    SceneStatus getSceneStatus() const;
    juce::File getSceneFolder() const { const juce::ScopedLock sl (sceneLock); return sceneFolder; }
    juce::String getSceneReport() const;                               // what the last load set and ignored
    juce::File getScenesFolder() const { return scenesFolder; }
    void setScenesFolder (const juce::File&);
    int  getLoadWhat() const { return loadWhat; }
    void setLoadWhat (int);                                            // 0 as the file says, 1 layers only, 2 scene, 3 settings only

private:
    ceo::scene::Settings currentSettings() const;
    void applySettings (const ceo::scene::Settings&);
    void setSceneLink (const juce::File& folder, const juce::String& layerMask);

    // Called on the main thread after a Capture recording filled its layer: switches the Capture button off.
    void handleAsyncUpdate() override;
    void timerCallback() override;          // 10 times a second: lets the file library move its work along

    ceo::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    ceo::Engine engine;
    ceo::LayerLibrary library { engine };   // after the engine: it is destroyed first
    juce::File homeFolder;
    juce::File scenesFolder;                // where the Scenes menu looks (per computer)
    int loadWhat = 0;                       // the Load menu (per computer)
    mutable juce::CriticalSection sceneLock;
    juce::File sceneFolder;                 // the Scene this project is linked to (empty = none)
    juce::String sceneMask;                 // the layers (digits 1..6) that came from / went to that Scene folder
    juce::String sceneReport, sceneMessage;
    uint32_t seenCaptureRanOut = 0;     // audio thread only: how many 'layer full' events were already passed on

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeoAudioProcessor)
};
