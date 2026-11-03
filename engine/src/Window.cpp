#include "ceo/Window.h"
#include "ceo/Mapping.h"

#include <algorithm>
#include <cmath>

namespace ceo
{
namespace
{

constexpr int kTableSize = 1024;
constexpr int kCompSteps = 16;

float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

struct Tables
{
    float bell[kTableSize + 1];                 // Gaussian bell, forced to 0 at both ends
    float soft[kTableSize + 1];                 // half-cosine rise 0 -> 1
    float comp[2][kCompSteps + 1];              // loudness compensation per shape and morph amount

    float lookup (const float* table, float x) const
    {
        const float p = clampf (x, 0.0f, 1.0f) * (float) kTableSize;
        const int   i = std::min ((int) p, kTableSize - 1);
        const float f = p - (float) i;
        return table[i] + f * (table[i + 1] - table[i]);
    }

    float evaluate (float morph, bool ramp, float fadeSq, float fadeRmp, float t) const
    {
        const float bellValue = lookup (bell, t);
        if (morph <= 0.0f)
            return bellValue;

        float other;
        if (! ramp)
        {
            const float a = t < fadeSq          ? lookup (soft, t / fadeSq)          : 1.0f;
            const float b = (1.0f - t) < fadeSq ? lookup (soft, (1.0f - t) / fadeSq) : 1.0f;
            other = a * b;
        }
        else
        {
            const float a = t < fadeRmp ? lookup (soft, t / fadeRmp) : 1.0f;
            other = a * (1.0f - t);
        }
        return bellValue + morph * (other - bellValue);
    }

    Tables()
    {
        const float sigma = 0.15f;
        const float edge  = std::exp (-0.5f * (0.5f / sigma) * (0.5f / sigma));

        for (int i = 0; i <= kTableSize; ++i)
        {
            const float t = (float) i / (float) kTableSize;
            const float g = std::exp (-0.5f * ((t - 0.5f) / sigma) * ((t - 0.5f) / sigma));
            bell[i] = (g - edge) / (1.0f - edge);
            soft[i] = 0.5f * (1.0f - std::cos (map::kPi * t));
        }

        // Average loudness (RMS) of each shape at typical edge settings, so we can even it out.
        for (int shape = 0; shape < 2; ++shape)
            for (int s = 0; s <= kCompSteps; ++s)
            {
                const float morph = (float) s / (float) kCompSteps;
                const int   steps = 512;
                double sum = 0.0;
                for (int n = 0; n < steps; ++n)
                {
                    const float w = evaluate (morph, shape == 1, 0.02f, 0.02f, ((float) n + 0.5f) / (float) steps);
                    sum += (double) w * (double) w;
                }
                const float rms = (float) std::sqrt (sum / steps);
                comp[shape][s] = 1.0f / std::max (rms, 0.2f);
            }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

} // namespace

void GrainWindow::set (float knob, float lengthSeconds)
{
    knob = map::clamp01 (knob);
    const float d = (knob - 0.5f) * 2.0f;                // -1 (square) .. +1 (ramp)

    if (std::fabs (d) < 0.02f)
        morph = 0.0f;
    else
        morph = (std::fabs (d) - 0.02f) / 0.98f;

    ramp = d > 0.0f;

    // Soft edges last about 2 ms, but never less than 2% of the grain (and never more than half).
    const float len = std::max (lengthSeconds, 0.001f);
    fadeSq  = clampf (0.002f / len, 0.02f, 0.5f);
    fadeRmp = clampf (0.002f / len, 0.01f, 0.5f);

    const Tables& t = tables();
    const float p = morph * (float) kCompSteps;
    const int   i = std::min ((int) p, kCompSteps - 1);
    const float f = p - (float) i;
    const float* c = t.comp[ramp ? 1 : 0];
    comp = c[i] + f * (c[i + 1] - c[i]);
}

float GrainWindow::at (float t) const
{
    return tables().evaluate (morph, ramp, fadeSq, fadeRmp, t);
}

} // namespace ceo
