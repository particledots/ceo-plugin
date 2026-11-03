// Ceo engine: how each knob position (0.0 to 1.0) turns into a real-world value.
//
// All of this is our own original maths. The Arbhar manual was used only to
// understand what each control is meant to do (for example "about 4 ms to 3 s,
// 750 ms at centre"); the curves and constants below are our own choices.
//
// Header only, no dependencies, so the tests can check every curve directly.

#pragma once

#include <algorithm>
#include <cmath>

namespace ceo
{
namespace map
{

constexpr float kPi = 3.14159265358979f;

inline float clamp01 (float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline float dbToGain (float db)
{
    return std::pow (10.0f, db / 20.0f);
}

// Input level: silent at 0, 0 dB (unchanged) at centre, +18 dB at full.
inline float inputGain (float k)
{
    k = clamp01 (k);
    if (k < 0.002f)
        return 0.0f;
    return dbToGain (k <= 0.5f ? -60.0f + 120.0f * k : 36.0f * (k - 0.5f));
}

// Output level: silent at 0, 0 dB (unchanged) at centre, +12 dB at full.
inline float outputGain (float k)
{
    k = clamp01 (k);
    if (k < 0.002f)
        return 0.0f;
    return dbToGain (k <= 0.5f ? -60.0f + 120.0f * k : 24.0f * (k - 0.5f));
}

// Dry/wet: equal-power crossfade. 0 = all dry, 1 = all granular.
inline float dryGain (float mix) { return std::cos (0.5f * kPi * clamp01 (mix)); }
inline float wetGain (float mix) { return std::sin (0.5f * kPi * clamp01 (mix)); }

// Soft saturation: signals below 0.7 pass untouched, louder ones are bent
// smoothly towards (never beyond) +/-1 instead of being chopped off.
inline float softSat (float x)
{
    const float a = std::fabs (x);
    if (a <= 0.7f)
        return x;
    const float y = 0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f);
    return x < 0.0f ? -y : y;
}

// Length: 4 ms at the bottom, 750 ms at centre, 3 s at the top, exponential feel.
// (The very bottom becomes Wavetable mode in milestone 5; until then it is just 4 ms.)
inline float grainLengthSeconds (float k)
{
    k = clamp01 (k);
    if (k <= 0.5f)
        return 0.004f * std::pow (0.75f / 0.004f, k / 0.5f);
    return 0.75f * std::pow (4.0f, (k - 0.5f) / 0.5f);
}

// Pitch: centre = no change (with a small flat spot so centre is exactly in
// tune), full travel = plus or minus 2 octaves.
inline float pitchSemitones (float k)
{
    const float d = (clamp01 (k) - 0.5f) * 2.0f;      // -1 .. +1
    const float a = std::fabs (d);
    if (a < 0.02f)
        return 0.0f;
    const float s = 24.0f * (a - 0.02f) / 0.98f;
    return d < 0.0f ? -s : s;
}

inline float pitchRatio (float k)
{
    return std::exp2 (pitchSemitones (k) / 12.0f);
}

// Intensity: how many grains overlap, and whether they are evenly spaced.
struct IntensityMap
{
    bool  active = true;   // false at both extremes (continuous engine off)
    bool  sync   = true;   // true: evenly timed, equal level. false: random timing and level
    float grains = 44.0f;  // average number of grains sounding at once
};

inline IntensityMap intensity (float k)
{
    k = clamp01 (k);
    const float d = std::fabs (k - 0.5f) * 2.0f;      // 0 at centre, 1 at the ends

    IntensityMap m;
    m.sync = (k <= 0.5f);

    if (d >= 0.97f)
    {
        m.active = false;
        m.grains = 1.0f;
        return m;
    }

    m.active = true;
    m.grains = std::pow (44.0f, 1.0f - d / 0.97f);    // 44 at centre, 1 just before the ends
    return m;
}

// Grain direction: chance (0 to 1) that a grain plays backwards. 50% at centre.
inline float reverseProbability (float k)
{
    return clamp01 (k);
}

// Strike grains are a little louder than continuous grains so they work as accents.
constexpr float kStrikeGain = 1.15f;

// Dub: how much of the old layer audio is kept when recording over it.
// 0 = none (replace), centre = half level, full = full level (sound on sound).
// Small flat spots at both ends so "replace" and "full" are exact.
inline float dubGain (float k)
{
    k = clamp01 (k);
    if (k < 0.02f)
        return 0.0f;
    if (k > 0.98f)
        return 1.0f;
    return k;
}

// Pitch deviation: a random pitch offset (in semitones) for one grain.
// 'u' is a fresh random number from 0.0 (inclusive) to 1.0 (exclusive).
//   Centre            : none.
//   Turned down       : free (unquantised) random offset, up to plus or minus 12 semitones at the bottom.
//   Turned up         : random offset in whole semitone steps; the reach grows from
//                       plus or minus 1 semitone just above centre to plus or minus 12 at the top.
inline float pitchDeviationSemitones (float k, float u)
{
    const float d = (clamp01 (k) - 0.5f) * 2.0f;      // -1 .. +1
    const float a = std::fabs (d);
    if (a < 0.02f)
        return 0.0f;

    const float amount = (a - 0.02f) / 0.98f;         // 0 .. 1

    if (d < 0.0f)
        return (2.0f * u - 1.0f) * 12.0f * amount;

    const int reach = 1 + (int) (amount * 11.999f);   // 1 .. 12
    int step = (int) (u * (float) (2 * reach + 1));
    if (step > 2 * reach)
        step = 2 * reach;
    return (float) (step - reach);
}

// Follow mode: the Scan knob becomes a speed control for the playhead.
// Samples of layer passed per output sample: 0 (stalled) in a small flat spot at centre,
// then from 1/20 speed up to 20x speed, exponential. Above centre = forwards, below = backwards.
inline float followSpeed (float k)
{
    const float d = (clamp01 (k) - 0.5f) * 2.0f;      // -1 .. +1
    const float a = std::fabs (d);
    if (a < 0.04f)
        return 0.0f;
    const float s = 0.05f * std::pow (400.0f, (a - 0.04f) / 0.96f);   // 1/20 .. 20
    return d < 0.0f ? -s : s;
}

// ---------------------------------------------------------------------------
// Milestone 5
// ---------------------------------------------------------------------------

// Hold: about 5 ms at the bottom, 2.5 s at centre, 10 s at the top, exponential feel.
inline float holdSeconds (float k)
{
    k = clamp01 (k);
    if (k <= 0.5f)
        return 0.005f * std::pow (2.5f / 0.005f, k / 0.5f);
    return 2.5f * std::pow (4.0f, (k - 0.5f) / 0.5f);
}

// Sense in Stereo input mode: the level of the right-hand input, same curve as Input level
// (the two inputs are matched when both knobs are at centre).
inline float senseGain (float k) { return inputGain (k); }

// Onset sensitivity (Sense in Mono input mode). The detector compares how much the
// sound's spectrum has just changed with how much it has been changing lately:
//   an onset needs  change > recent average * ratio + floor.
// Turning Sense up lowers both numbers, so quieter and softer attacks are caught.
struct OnsetMap
{
    float ratio;
    float floorFlux;
};

inline OnsetMap onsetSensitivity (float k)
{
    k = clamp01 (k);
    OnsetMap m;
    m.ratio     = 5.0f * std::pow (0.4f, k);          // 5.0 at the bottom, 2.0 at the top
    m.floorFlux = 30.0f * std::pow (0.02f, k);        // 30 at the bottom, 0.6 at the top
    return m;
}

// Mod amount: centre = off. Returns -1 .. +1 (small flat spot at centre so "off" is exact).
inline float modBipolar (float k)
{
    const float d = (clamp01 (k) - 0.5f) * 2.0f;
    const float a = std::fabs (d);
    if (a < 0.02f)
        return 0.0f;
    const float s = (a - 0.02f) / 0.98f;
    return d < 0.0f ? -s : s;
}

// Mod target Hold: the Mod amount is added to the Hold knob (a full turn of Mod moves it half the knob's travel).
inline float holdKnobWithMod (float holdKnob, float modD)
{
    return clamp01 (holdKnob + 0.5f * modD);
}

// Mod target Pan. Where one grain (or note) sits between left and right.
//   Centre : spread randomly across the whole stereo field (equal power).
//   Up     : the spread narrows to the middle (by half way), then the stereo image of the
//            recording itself is folded down to mono (full up = fully mono).
//   Down   : the grains are pushed out to hard left or hard right (coin toss) at the bottom.
// u1, u2 are fresh random numbers 0..1.  angle: 0 = left, pi/2 = right.
struct PanSettings
{
    float angle;
    float monoBlend;     // 0 = keep the recording's own stereo image, 1 = fold it to mono
};

inline PanSettings panSettings (float modD, float u1, float u2)
{
    const float uniform = u1 * 0.5f * kPi;
    PanSettings p { uniform, 0.0f };

    if (modD < 0.0f)
    {
        const float coin = u2 < 0.5f ? 0.0f : 0.5f * kPi;
        p.angle = uniform + (-modD) * (coin - uniform);
    }
    else if (modD > 0.0f)
    {
        const float narrow = std::min (1.0f, modD / 0.5f);
        p.angle     = uniform + narrow * (0.25f * kPi - uniform);
        p.monoBlend = modD > 0.5f ? (modD - 0.5f) / 0.5f : 0.0f;
    }
    return p;
}

// Mod target Reverb.
//   Up   : reverb mixed in with the sound; longer decay as it goes up; frozen (endless) at the very top.
//   Down : the direct sound fades out and only the reverb is heard, with a slow shimmering movement
//          that grows as it goes down; longer decay as it goes down; frozen at the very bottom.
struct ReverbMap
{
    bool  active  = false;
    bool  freeze  = false;
    float decay   = 1.0f;     // seconds (RT60)
    float dry     = 1.0f;
    float wet     = 0.0f;
    float shimmer = 0.0f;     // 0..1
};

inline ReverbMap reverbMap (float modD)
{
    ReverbMap m;
    const float a = std::fabs (modD);
    if (a <= 0.0f)
        return m;

    m.active = true;
    m.freeze = a > 0.97f;
    m.decay  = 0.3f * std::pow (200.0f, std::min (a, 0.97f) / 0.97f);     // 0.3 s .. 60 s

    if (modD > 0.0f)
    {
        m.dry = 1.0f;
        m.wet = 0.6f * std::min (1.0f, a / 0.3f);
    }
    else
    {
        const float t = std::min (1.0f, a / 0.08f);
        m.dry     = 1.0f - t;
        m.wet     = 0.6f + 0.4f * t;
        m.shimmer = a;
    }
    return m;
}

// Mod target Delay.
//   Up   : echoes, 80 ms to 1 s, steady 67% feedback.
//   Down : very short delays, 25 ms down to 1 ms, 99% feedback, giving metallic, pitched
//          resonances (the time sets the pitch).
struct DelayMap
{
    bool  active   = false;
    bool  metallic = false;
    float time     = 0.0f;    // seconds
    float feedback = 0.0f;
    float inGain   = 1.0f;    // keeps the loud 99% feedback setting from being much louder than the echoes
    float level    = 0.0f;    // how much of the delay is added to the sound
};

inline DelayMap delayMap (float modD)
{
    DelayMap m;
    const float a = std::fabs (modD);
    if (a <= 0.0f)
        return m;

    const float fade = std::min (1.0f, a / 0.08f);     // fades in just off centre, so there is no click
    m.active = true;
    if (modD > 0.0f)
    {
        m.time     = 0.08f * std::pow (12.5f, a);       // 80 ms .. 1 s
        m.feedback = 0.67f;
        m.inGain   = 1.0f;
        m.level    = 0.7f * fade;
    }
    else
    {
        m.metallic = true;
        m.time     = 0.025f * std::pow (0.04f, a);      // 25 ms .. 1 ms
        m.feedback = 0.99f;
        m.inGain   = 0.35f;
        m.level    = 1.0f * fade;
    }
    return m;
}

// Wavetable mode: Length fully down crossfades from grains into an oscillator.
// Returns 1 = all oscillator (knob at the very bottom) .. 0 = all grains (knob above 6%).
inline float wavetableMix (float lengthKnob)
{
    const float edge = 0.06f;
    const float k = clamp01 (lengthKnob);
    if (k >= edge)
        return 0.0f;
    const float t = k / edge;
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

// Centre pitch of the oscillator (Pitch knob at centre): C2.
constexpr float kWavetableCentreHz = 130.8128f;

// In Wavetable mode the Grain direction knob sets how quickly one wavetable melts into the next
// (10 ms at the bottom, about 3 s at the top).
inline float wavetableMorphSeconds (float directionKnob)
{
    return 0.01f * std::pow (300.0f, clamp01 (directionKnob));
}

} // namespace map
} // namespace ceo
