// Ceo audio engine: public interface.
//
// This folder knows nothing about plugins, JUCE, Logic or sliders. The plugin
// wrapper (plugin/) hands it a set of numbers and a block of audio; the engine
// hands the audio back. Keeping it separate means the same engine code can be
// reused for other plugins later.
//
// Milestone 4: all six layers (alpha to zeta) and Omega (all six end to end along
// Scan), Capture into the selected layer with Dub (replace / sound on sound),
// one-step Undo and Erase, Strike grains, Pitch deviation and Follow mode.
// Still to come:
//   - Mod amount, Hold, Sense, onset detection, Wavetable mode: milestone 5

#pragma once

#include <memory>

namespace ceo
{

constexpr int   kNumLayers            = 6;      // alpha to zeta
constexpr float kLayerSeconds         = 10.0f;  // recording time of one layer
constexpr float kTailSeconds          = 3.0f;   // extra seconds recorded after the end, so long grains near the end do not drop out
constexpr int   kMaxGrains            = 88;     // both grain engines together
constexpr int   kMaxContinuousGrains  = 44;     // continuous engine's share
constexpr int   kMaxStrikeGrains      = 44;     // Strike engine's share

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
    bool  capture        = false;  // on = record into the selected layer, off = stop
    bool  strike         = false;  // every change (a click on, or a click off) fires one Strike grain
    bool  erase          = false;  // every change clears the selected layer
    bool  undo           = false;  // every change undoes the last record-over (one step)
    bool  follow         = false;  // Follow mode: Scan becomes a speed control
    int   layer          = 0;      // 0..5 = alpha..zeta, 6 = Omega (all six, end to end along Scan)

    // Grain controls
    float intensity      = 0.5f;
    float grainWindow    = 0.5f;
    float grainDirection = 0.5f;
    float length         = 0.5f;
    float scan           = 0.0f;   // in Follow mode: playhead speed
    float spray          = 0.0f;
    float pitch          = 0.5f;
    float pitchDeviation = 0.5f;
    float modAmount      = 0.0f;   // milestone 5
    float hold           = 0.5f;   // milestone 5
    float sense          = 0.5f;   // milestone 5
    float dub            = 0.5f;
};

class Engine
{
public:
    Engine();
    ~Engine();

    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    // Called before audio starts, and again if the sample rate or block size changes.
    // Allocates memory, so it is never called from the audio thread. Recordings
    // in the layers are kept unless the sample rate changed.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    // Stop recording and silence all grains (called when playback restarts).
    // The recorded layers themselves are kept.
    void reset();

    // Give the engine the current control values. Called once per audio block.
    void setParams (const Params& newParams);

    // Process audio in place. channels[c] points at numSamples floats for channel c.
    // Mono (1 channel) or stereo (2 channels). Must not allocate memory, lock,
    // or do anything slow.
    void process (float* const* channels, int numChannels, int numSamples);

    const Params& getParams() const { return params; }

    // Read-only status (used by the self-tests now, and by the stream display later).
    bool   isRecording() const;
    int    recordingLayer() const;            // 0..5, or -1 when not recording
    bool   layerHasContent() const;           // any layer
    bool   layerHasContent (int layer) const; // one layer, 0..5
    int    undoLayer() const;                 // the layer Undo would restore, or -1
    int    activeGrainCount() const;          // continuous and Strike grains together
    int    activeStrikeGrainCount() const;
    double playheadSeconds() const;           // Follow mode playhead, from the start of the layer (or of alpha in Omega)

private:
    struct Impl;

    Params params;
    double sampleRate    = 44100.0;
    int    maxBlockSize  = 0;
    int    channelCount  = 0;
    std::unique_ptr<Impl> impl;
};

} // namespace ceo
