// Ceo engine: how each knob position (0.0 to 1.0) turns into a real-world value.
//
// All of this is our own original maths. The Arbhar manual was used only to
// understand what each control is meant to do (for example "about 4 ms to 3 s,
// 750 ms at centre"); the curves and constants below are our own choices.
//
// Header only, no dependencies, so the tests can check every curve directly.

#pragma once

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

} // namespace map
} // namespace ceo
