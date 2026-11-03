// Ceo engine: the two effects behind the Mod section. Both are our own designs.
//
//   Reverb:      eight delay lines that feed each other through a mixing matrix (a feedback
//                delay network), preceded by short smearing filters. The decay time sets how
//                much each trip round the loop loses; "freeze" stops new sound entering and
//                loses nothing, so the tail holds forever. "Shimmer" slowly wobbles the delay
//                times so the tail moves.
//   StereoDelay: one delay line per side with feedback. Long settings give echoes; very short
//                settings with strong feedback ring like a plucked string (metallic).

#pragma once

#include <vector>

namespace ceo
{

class Reverb
{
public:
    void prepare (double sampleRate);     // allocates: not from the audio thread
    void reset();

    // decaySeconds: time for the tail to fall by 60 dB. freeze: hold the tail forever.
    // shimmer 0..1: how much the delay times wobble.
    void set (float decaySeconds, bool freeze, float shimmer);

    void process (float inL, float inR, float& outL, float& outR);

private:
    static constexpr int kLines = 8;

    struct Line
    {
        std::vector<float> buf;
        int   size = 1;
        int   w = 0;
        int   length = 1;            // nominal loop length in samples
        float lp = 0.0f;             // damping filter state
        float gain = 0.0f;           // current feedback gain (glides to the target)
        float gainTarget = 0.0f;
        float lfoPhase = 0.0f;
        float lfoInc = 0.0f;
    };

    struct Allpass
    {
        std::vector<float> buf;
        int idx = 0;
        float process (float x, float g);
    };

    double sr = 48000.0;
    Line   lines[kLines];
    Allpass apL[2], apR[2];
    float  damp       = 0.4f;
    float  normalDamp = 0.4f;
    float  dampTarget = 0.4f;
    float  modDepth   = 0.0f;        // samples
    float  modTarget  = 0.0f;
    float  inGain     = 1.0f;
    float  inGainTarget = 1.0f;
    float  glide      = 0.001f;
};

class StereoDelay
{
public:
    void prepare (double sampleRate);     // allocates: not from the audio thread
    void reset();

    // timeSeconds: delay (up to 1.1 s). feedback: 0..0.995. damping 0..1 (higher = darker repeats).
    // inGain: level fed into the line. rightRatio: right side's time compared with the left.
    void set (float timeSeconds, float feedback, float damping, float inGain, float rightRatio);

    void process (float inL, float inR, float& outL, float& outR);

private:
    float readLinear (const std::vector<float>& buf, float delaySamples) const;

    double sr = 48000.0;
    std::vector<float> bufL, bufR;
    int   size = 1;
    int   w = 0;
    float time = 0.1f, timeTarget = 0.1f, timeCoef = 0.001f;
    float feedback = 0.5f, damping = 0.5f, inGain = 1.0f, rightRatio = 1.0f;
    float lpL = 0.0f, lpR = 0.0f;
    float hpInL = 0.0f, hpOutL = 0.0f, hpInR = 0.0f, hpOutR = 0.0f;
};

} // namespace ceo
