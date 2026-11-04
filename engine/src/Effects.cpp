#include "ceo/Effects.h"

#include <algorithm>
#include <cmath>

namespace ceo
{
namespace
{

inline float flush (float x)
{
    return std::fabs (x) < 1e-18f ? 0.0f : x;
}

// Loop lengths at 48 kHz (all different, no common factors) and the diffusion filter lengths.
const int kLineLengths[8] = { 1117, 1283, 1459, 1621, 1867, 2063, 2287, 2539 };
const int kApLengthsL[2]  = { 113, 241 };
const int kApLengthsR[2]  = { 131, 257 };

constexpr float kOutScale = 0.5f;

} // namespace

// ---------------------------------------------------------------------------
// Reverb
// ---------------------------------------------------------------------------

float Reverb::Allpass::process (float x, float g)
{
    const float d = buf[(size_t) idx];
    const float v = x + g * d;
    buf[(size_t) idx] = flush (v);
    if (++idx >= (int) buf.size())
        idx = 0;
    return d - g * v;
}

void Reverb::prepare (double sampleRate)
{
    sr = sampleRate;
    const double scale = sr / 48000.0;
    const int    maxMod = (int) std::ceil (60.0 * scale);

    for (int i = 0; i < kLines; ++i)
    {
        Line& l = lines[i];
        l.length = std::max (16, (int) std::lround ((double) kLineLengths[i] * scale));
        l.size   = l.length + maxMod + 8;
        l.buf.assign ((size_t) l.size, 0.0f);
        // Slow wobbles, each line at its own rate (0.6 to 1.5 Hz).
        l.lfoInc = (float) ((0.6 + 0.13 * (double) i) / sr);
        l.lfoPhase = (float) i / (float) kLines;
    }

    for (int k = 0; k < 2; ++k)
    {
        apL[k].buf.assign ((size_t) std::max (8, (int) std::lround ((double) kApLengthsL[k] * scale)), 0.0f);
        apR[k].buf.assign ((size_t) std::max (8, (int) std::lround ((double) kApLengthsR[k] * scale)), 0.0f);
    }

    normalDamp = 1.0f - std::exp (-2.0f * 3.14159265f * 5000.0f / (float) sr);
    damp       = normalDamp;
    glide = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));

    reset();
    set (1.0f, false, 0.0f);
    for (auto& l : lines)
        l.gain = l.gainTarget;
    modDepth = modTarget;
    inGain = inGainTarget;
    damp = dampTarget;
}

void Reverb::reset()
{
    for (auto& l : lines)
    {
        std::fill (l.buf.begin(), l.buf.end(), 0.0f);
        l.w = 0;
        l.lp = 0.0f;
    }
    for (int k = 0; k < 2; ++k)
    {
        std::fill (apL[k].buf.begin(), apL[k].buf.end(), 0.0f);
        std::fill (apR[k].buf.begin(), apR[k].buf.end(), 0.0f);
        apL[k].idx = apR[k].idx = 0;
    }
}

void Reverb::set (float decaySeconds, bool freeze, float shimmer)
{
    decaySeconds = std::max (0.05f, decaySeconds);
    for (auto& l : lines)
    {
        // Each trip round a loop of N samples should lose 60 dB per 'decaySeconds'.
        l.gainTarget = freeze ? 1.0f : std::pow (10.0f, -3.0f * (float) l.length / (decaySeconds * (float) sr));
    }
    // A longer decay stores more energy from a steady sound (the level grows with the square root of the
    // decay time), so the input is turned down to match: the reverb stays about as loud at 60 s as at 0.6 s.
    inGainTarget = freeze ? 0.0f : std::min (1.0f, std::sqrt (0.6f / decaySeconds));
    dampTarget   = freeze ? 1.0f : normalDamp;       // frozen: nothing is filtered away, so the tail never fades
    modTarget    = std::max (0.0f, std::min (1.0f, shimmer)) * 48.0f * (float) (sr / 48000.0);
}

void Reverb::process (float inL, float inR, float& outL, float& outR)
{
    inGain   += (inGainTarget - inGain) * glide;
    modDepth += (modTarget - modDepth) * glide;
    damp     += (dampTarget - damp) * glide;

    // Smear the input a little first.
    float dl = inL, dr = inR;
    dl = apL[0].process (dl, 0.6f);
    dl = apL[1].process (dl, 0.6f);
    dr = apR[0].process (dr, 0.6f);
    dr = apR[1].process (dr, 0.6f);
    dl *= inGain * 0.5f;
    dr *= inGain * 0.5f;

    float y[kLines];
    for (int i = 0; i < kLines; ++i)
    {
        Line& l = lines[i];
        l.lfoPhase += l.lfoInc;
        if (l.lfoPhase >= 1.0f)
            l.lfoPhase -= 1.0f;

        const float delay = (float) l.length + modDepth * std::sin (6.2831853f * l.lfoPhase);
        float p = (float) l.w - delay;
        while (p < 0.0f)
            p += (float) l.size;
        int i0 = (int) p;
        const float f = p - (float) i0;
        if (i0 >= l.size) i0 -= l.size;
        int i1 = i0 + 1;
        if (i1 >= l.size) i1 = 0;
        y[i] = l.buf[(size_t) i0] + f * (l.buf[(size_t) i1] - l.buf[(size_t) i0]);
    }

    // Damp (soften the highs a little on every trip), apply the loop gain, then mix the lines
    // together (a Householder reflection: it moves energy between the lines without changing the total).
    float v[kLines];
    float sum = 0.0f;
    for (int i = 0; i < kLines; ++i)
    {
        Line& l = lines[i];
        l.gain += (l.gainTarget - l.gain) * glide;
        l.lp = flush (l.lp + damp * (y[i] - l.lp));
        v[i] = l.gain * l.lp;
        sum += v[i];
    }
    sum *= 0.25f;

    for (int i = 0; i < kLines; ++i)
    {
        Line& l = lines[i];
        const float input = (i & 1) ? dr : dl;
        l.buf[(size_t) l.w] = flush (input + v[i] - sum);
        if (++l.w >= l.size)
            l.w = 0;
    }

    outL = kOutScale * (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]);
    outR = kOutScale * (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]);
}

// ---------------------------------------------------------------------------
// Stereo delay
// ---------------------------------------------------------------------------

void StereoDelay::prepare (double sampleRate)
{
    sr   = sampleRate;
    size = (int) std::ceil (1.25 * sr * 1.1) + 16;
    bufL.assign ((size_t) size, 0.0f);
    bufR.assign ((size_t) size, 0.0f);
    timeCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
    reset();
}

void StereoDelay::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    w = 0;
    lpL = lpR = 0.0f;
    hpInL = hpOutL = hpInR = hpOutR = 0.0f;
    time = timeTarget;
}

void StereoDelay::set (float timeSeconds, float fb, float damp, float in, float ratio)
{
    timeTarget = std::max (0.0005f, std::min (1.1f, timeSeconds));
    feedback   = std::max (0.0f, std::min (0.995f, fb));
    damping    = std::max (0.0f, std::min (1.0f, damp));
    inGain     = in;
    rightRatio = ratio;
}

float StereoDelay::readLinear (const std::vector<float>& buf, float delaySamples) const
{
    delaySamples = std::max (1.0f, std::min ((float) (size - 3), delaySamples));
    float p = (float) w - delaySamples;
    if (p < 0.0f)
        p += (float) size;
    int i0 = (int) p;
    const float f = p - (float) i0;
    if (i0 >= size) i0 -= size;
    int i1 = i0 + 1;
    if (i1 >= size) i1 = 0;
    return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
}

void StereoDelay::process (float inL, float inR, float& outL, float& outR)
{
    time += (timeTarget - time) * timeCoef;

    const float yL = readLinear (bufL, time * (float) sr);
    const float yR = readLinear (bufR, time * rightRatio * (float) sr);

    // Feedback path: soften the highs a little, and remove very low rumble so it cannot build up.
    lpL = flush (lpL + (1.0f - damping) * (yL - lpL));
    lpR = flush (lpR + (1.0f - damping) * (yR - lpR));
    const float fbL = lpL - hpInL + 0.995f * hpOutL;
    const float fbR = lpR - hpInR + 0.995f * hpOutR;
    hpInL = lpL;  hpOutL = flush (fbL);
    hpInR = lpR;  hpOutR = flush (fbR);

    bufL[(size_t) w] = flush (inL * inGain + hpOutL * feedback);
    bufR[(size_t) w] = flush (inR * inGain + hpOutR * feedback);
    if (++w >= size)
        w = 0;

    outL = yL;
    outR = yR;
}

} // namespace ceo
