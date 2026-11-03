// Ceo audio engine: public interface.
//
// This folder knows nothing about plugins, JUCE, Logic or sliders. The plugin
// wrapper (plugin/) hands it a set of numbers and a block of audio; the engine
// hands the audio back. Keeping it separate means the same engine code can be
// reused for other plugins later.
//
// Milestone 3: first sound. Layer alpha can be recorded with Capture, and
// continuous grains are played from it, shaped by Length, Scan, Spray,
// Intensity, Grain window, Grain direction and Pitch. Input level, Dry/wet and
// Output level work. Everything else in Params is stored but not used yet:
//   - Strike, Pitch deviation, the other layers and Omega, Dub, Follow mode: milestone 4
//   - Mod amount, Hold, Sense, onset detection, Wavetable mode: milestone 5

#pragma once

#include <memory>

namespace ceo
{

constexpr float kLayerSeconds         = 10.0f;  // recording time of one layer
constexpr float kTailSeconds          = 3.0f;   // extra seconds recorded after the end, so long grains near the end do not drop out
constexpr int   kMaxGrains            = 88;     // both grain engines together
constexpr int   kMaxContinuousGrains  = 44;     // continuous engine's share

// All control values the engine can be given. Knobs run from 0.0 to 1.0
// (0.5 is the centre position). Every value is "as the knob is set"; the engine
// itself decides what each one means in seconds, percent, semitones, etc.
// (see Mapping.h).
struct Params
{
    // Top row
    float inputLevel     = 0.5f;
    float dryWet         = 0.5f;
    float outputLevel    = 0.5f;

    // Buttons and layer dial
    bool  capture        = false;  // start/stop recording into the selected layer
    bool  strike         = false;  // fire one grain (milestone 4)
    int   layer          = 0;      // 0..5 = alpha..zeta, 6 = Omega (all six). Only 0 works in milestone 3.

    // Grain controls
    float intensity      = 0.5f;
    float grainWindow    = 0.5f;
    float grainDirection = 0.5f;
    float length         = 0.5f;
    float scan           = 0.0f;
    float spray          = 0.0f;
    float pitch          = 0.5f;
    float pitchDeviation = 0.5f;   // milestone 4
    float modAmount      = 0.0f;   // milestone 5
    float hold           = 0.5f;   // milestone 5
    float sense          = 0.5f;   // milestone 5
    float dub            = 0.5f;   // milestone 4
};

class Engine
{
public:
    Engine();
    ~Engine();

    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    // Called before audio starts, and again if the sample rate or block size changes.
    // Allocates memory, so it is never called from the audio thread. A recording
    // already in layer alpha is kept unless the sample rate changed.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    // Stop recording and silence all grains (called when playback restarts).
    // The recorded layer itself is kept.
    void reset();

    // Give the engine the current control values. Called once per audio block.
    void setParams (const Params& newParams);

    // Process audio in place. channels[c] points at numSamples floats for channel c.
    // Mono (1 channel) or stereo (2 channels). Must not allocate memory, lock,
    // or do anything slow.
    void process (float* const* channels, int numChannels, int numSamples);

    const Params& getParams() const { return params; }

    // Read-only status (used by the self-tests now, and by the stream display later).
    bool isRecording() const;
    bool layerHasContent() const;
    int  activeGrainCount() const;

private:
    struct Impl;

    Params params;
    double sampleRate    = 44100.0;
    int    maxBlockSize  = 0;
    int    channelCount  = 0;
    std::unique_ptr<Impl> impl;
};

} // namespace ceo
