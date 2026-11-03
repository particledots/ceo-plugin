// Ceo audio engine: public interface.
//
// This folder knows nothing about plugins, JUCE, Logic or sliders. The plugin
// wrapper (plugin/) hands it a set of numbers and a block of audio; the engine
// hands the audio back. Keeping it separate means the same engine code can be
// reused for other plugins later.
//
// Milestone 2: the engine passes audio through completely unchanged. The control
// values are stored but not used yet. The granular sound arrives in milestone 3.

#pragma once

namespace ceo
{

// All control values the engine can be given. Knobs run from 0.0 to 1.0
// (0.5 is the centre position). Every value is "as the knob is set"; the engine
// itself decides later what each one means in seconds, percent, semitones, etc.
struct Params
{
    // Top row
    float inputLevel     = 0.5f;
    float dryWet         = 0.5f;
    float outputLevel    = 0.5f;

    // Buttons and layer dial
    bool  capture        = false;  // start/stop recording into the selected layer
    bool  strike         = false;  // fire one grain
    int   layer          = 0;      // 0..5 = alpha..zeta, 6 = Omega (all six)

    // Grain controls
    float intensity      = 0.5f;
    float grainWindow    = 0.5f;
    float grainDirection = 0.5f;
    float length         = 0.5f;
    float scan           = 0.0f;
    float spray          = 0.0f;
    float pitch          = 0.5f;
    float pitchDeviation = 0.5f;
    float modAmount      = 0.0f;
    float hold           = 0.5f;
    float sense          = 0.5f;
    float dub            = 0.5f;
};

class Engine
{
public:
    Engine() = default;

    // Called before audio starts, and again if the sample rate or block size changes.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    // Forget any internal state (called when playback restarts).
    void reset();

    // Give the engine the current control values. Called once per audio block.
    void setParams (const Params& newParams);

    // Process audio in place. channels[c] points at numSamples floats for channel c.
    // Must not allocate memory, lock, or do anything slow.
    void process (float* const* channels, int numChannels, int numSamples);

    const Params& getParams() const { return params; }

private:
    Params params;
    double sampleRate    = 44100.0;
    int    maxBlockSize  = 0;
    int    channelCount  = 0;
};

} // namespace ceo
