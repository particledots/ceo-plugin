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
// Milestone 5: onset detection (Onset capture, Hold, Sense), Mono/Stereo input
// mode, the Mod section (Pan, Hold, Reverb, Delay) and Wavetable mode.
// Milestone 6: a gentler restart when the host prepares the engine again, and read-only
// information for the stream display (recent grain starts, a layer's loudness shape).

#pragma once

#include <cstdint>
#include <memory>

namespace ceo
{

constexpr int   kNumLayers            = 6;      // alpha to zeta
constexpr float kLayerSeconds         = 10.0f;  // recording time of one layer
constexpr float kTailSeconds          = 3.0f;   // extra seconds recorded after the end, so long grains near the end do not drop out
constexpr int   kMaxGrains            = 88;     // both grain engines together
constexpr int   kMaxContinuousGrains  = 44;     // continuous engine's share
constexpr int   kMaxStrikeGrains      = 44;     // Strike engine's share
constexpr int   kMaxWtNotes           = 8;      // Strike notes in Wavetable mode
constexpr float kPreRollSeconds       = 0.030f; // audio kept from just before a detected onset, so the attack is not clipped

// What the Mod amount knob controls (Params::modTarget).
enum ModTarget { kModPan = 0, kModHold = 1, kModReverb = 2, kModDelay = 3 };

// One grain that has just started, as reported to the stream display.
struct GrainStart
{
    float position = 0.0f;   // 0..1 along the layer (in Omega: along all six layers end to end)
    bool  strike   = false;  // started by the Strike button
    bool  reversed = false;  // plays backwards
};

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
    bool  stereoInput    = false;  // false = Mono input mode, true = Stereo input mode (left = input 1, right = input 2)
    bool  onsetCapture   = false;  // on = a detected onset starts recording into the selected layer (Mono input mode only)
    int   modTarget      = 0;      // ModTarget: 0 Pan, 1 Hold, 2 Reverb, 3 Delay

    // Grain controls
    float intensity      = 0.5f;
    float grainWindow    = 0.5f;
    float grainDirection = 0.5f;
    float length         = 0.5f;
    float scan           = 0.0f;   // in Follow mode: playhead speed
    float spray          = 0.0f;
    float pitch          = 0.5f;
    float pitchDeviation = 0.5f;
    float modAmount      = 0.5f;   // centre = off; above and below centre act on the Mod target
    float hold           = 0.5f;   // onset record time and re-arm time; Follow mode loop length
    float sense          = 0.5f;   // onset sensitivity (Mono input); level of the right-hand input (Stereo input)
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
    // in the layers are kept unless the sample rate changed. Called again with the
    // same sample rate and channel count (as hosts do when playback restarts), it
    // carries on gently: grains, the oscillator and reverb/echo tails keep sounding.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    // Stop recording and silence everything at once: all grains, the oscillator, the reverb and echo tails.
    // The recorded layers themselves are kept. (A host restart does not use this: see prepare().)
    void reset();

    // ---- Read-only information for the stream display ----
    // Safe to call from the plugin window's thread while audio is running.

    // Goes up whenever recorded audio changes (recording, Erase, Undo).
    uint32_t recordingRevision() const;

    // Grain starts since 'cursor' (pass 0 the first time; it is updated), oldest first. The engine remembers
    // the last 128; if more happened since the last call, or more than maxCount, the oldest are skipped.
    // Returns how many were written to 'out'.
    int takeGrainStarts (uint32_t& cursor, GrainStart* out, int maxCount) const;

    // The loudness shape of one layer's 10 seconds: 'points' values, each the level (RMS, 0 = silent) of one
    // equal slice. An empty layer gives zeros. Returns false if the layer number is wrong or the memory is
    // being re-allocated at this moment (try again on the next tick).
    bool layerEnvelope (int layer, float* out, int points) const;

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
    bool   layerIsStereo (int layer) const;   // true if that layer holds a two-channel recording
    int    onsetCount() const;                // onsets that started a recording since the engine was created
    bool   onsetArmed() const;                // true when a new onset would be accepted
    bool   wavetableActive() const;           // true while the oscillator is audible (continuous or Strike notes)

    // The recorded audio of one layer (channel 0 = left, 1 = right; the right side only holds
    // something different from the left in a stereo layer). nullptr if out of range. Read only.
    const float* layerAudio (int layer, int channel) const;
    int    layerAudioLength() const;          // samples in each of those arrays (10 s plus the tail)

private:
    struct Impl;

    Params params;
    double sampleRate    = 44100.0;
    int    maxBlockSize  = 0;
    int    channelCount  = 0;
    std::unique_ptr<Impl> impl;
};

} // namespace ceo
