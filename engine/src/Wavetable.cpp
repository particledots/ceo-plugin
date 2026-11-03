#include "ceo/Wavetable.h"

#include <algorithm>
#include <cmath>

namespace ceo
{
namespace
{

// Cubic (4-point) read of a table at a position 0..1 round the cycle.
inline float readTable (const WtTable& t, double phase)
{
    float p = (float) (phase * (double) kWtSize);
    int i = (int) p;
    if (i < 0) i = 0;
    if (i > kWtSize - 1) i = kWtSize - 1;
    const float f = p - (float) i;

    const float y0 = t.d[i], y1 = t.d[i + 1], y2 = t.d[i + 2], y3 = t.d[i + 3];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

} // namespace

int wtMaxHarmonic (double frequencyHz, double sampleRate)
{
    if (frequencyHz < 1.0)
        frequencyHz = 1.0;
    const int fit = (int) (0.45 * sampleRate / frequencyHz);   // highest harmonic below 45% of the sample rate
    if (fit >= 1024)
        return 1023;
    int h = 4;
    while (h * 2 <= fit)
        h *= 2;
    return h;
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

WtBuilder::WtBuilder() : fft (kWtSize), re ((size_t) kWtSize), im ((size_t) kWtSize), window ((size_t) kWtSize)
{
    // The first and last fifth of the slice fade in and out (sine squared), so the cycle joins up with itself.
    const float taper = 0.2f;
    for (int n = 0; n < kWtSize; ++n)
    {
        const float x = (float) n / (float) (kWtSize - 1);
        float w = 1.0f;
        if (x < taper)
            w = std::sin (0.5f * 3.14159265f * x / taper);
        else if (x > 1.0f - taper)
            w = std::sin (0.5f * 3.14159265f * (1.0f - x) / taper);
        window[(size_t) n] = w * w;
    }
}

void WtBuilder::analyse (const float* slice, WtSpectrum& out)
{
    double sum = 0.0;
    for (int n = 0; n < kWtSize; ++n)
    {
        const float x = slice[n];
        const float v = (std::isfinite (x) ? x : 0.0f) * window[(size_t) n];
        re[(size_t) n] = v;
        im[(size_t) n] = 0.0f;
        sum += (double) v * (double) v;
    }
    out.rms = (float) std::sqrt (sum / (double) kWtSize);
    fft.forward (re.data(), im.data());

    for (int k = 0; k < kWtBins; ++k)
    {
        out.re[k] = re[(size_t) k];
        out.im[k] = im[(size_t) k];
    }
    out.re[0] = out.im[0] = 0.0f;                          // no steady offset
    out.re[kWtBins - 1] = out.im[kWtBins - 1] = 0.0f;      // nothing exactly at the top
}

void WtBuilder::synthesise (const WtSpectrum& spectrum, int maxHarmonic, WtTable& out)
{
    maxHarmonic = std::max (1, std::min (kWtSize / 2 - 1, maxHarmonic));

    std::fill (re.begin(), re.end(), 0.0f);
    std::fill (im.begin(), im.end(), 0.0f);
    for (int k = 1; k <= maxHarmonic; ++k)
    {
        re[(size_t) k] = spectrum.re[k];
        im[(size_t) k] = spectrum.im[k];
        re[(size_t) (kWtSize - k)] =  spectrum.re[k];
        im[(size_t) (kWtSize - k)] = -spectrum.im[k];
    }
    fft.inverse (re.data(), im.data());

    // Dropping the highest harmonics also drops energy. Bring the cycle back to the loudness of the
    // slice it came from (within limits), so a bright recording is not much quieter than a dull one
    // and the level stays steady when the pitch moves into another band.
    double sum = 0.0;
    for (int n = 0; n < kWtSize; ++n)
        sum += (double) re[(size_t) n] * (double) re[(size_t) n];
    const float outRms = (float) std::sqrt (sum / (double) kWtSize);
    float lift = 1.0f;
    if (spectrum.rms > 1e-6f && outRms > 1e-9f)
        lift = std::max (0.0f, std::min (kWtMaxLift, spectrum.rms / outRms));
    for (int n = 0; n < kWtSize; ++n)
        re[(size_t) n] *= lift;

    for (int n = 0; n < kWtSize; ++n)
        out.d[n + 1] = re[(size_t) n];
    out.d[0]             = re[(size_t) (kWtSize - 1)];
    out.d[kWtSize + 1]   = re[0];
    out.d[kWtSize + 2]   = re[1];
}

// ---------------------------------------------------------------------------
// Voice
// ---------------------------------------------------------------------------

void WtVoice::prepare (double sampleRate)
{
    sr = sampleRate;
    freqCoef = 1.0 - std::exp (-1.0 / (0.005 * sr));
    clear();
}

void WtVoice::clear()
{
    has = false;
    morphing = false;
    fade = 0.0f;
    harm = 0;
    cur = 0;
    phase = 0.0;
}

void WtVoice::setFrequency (double hz, bool immediately)
{
    freqTarget = std::max (1.0, std::min (0.45 * sr, hz));
    if (immediately)
        freq = freqTarget;
}

// Folds an unfinished melt into the current table, so a new melt can start from what is audible now.
void WtVoice::bake()
{
    if (! morphing)
        return;

    const int nxt = 1 - cur;
    if (slotStereo[nxt] && ! slotStereo[cur])
    {
        std::copy (tab[cur][0].d, tab[cur][0].d + kWtSize + 3, tab[cur][1].d);
        slotStereo[cur] = true;
    }
    const int channels = slotStereo[cur] ? (slotStereo[nxt] ? 2 : 1) : 1;
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < kWtSize + 3; ++i)
            tab[cur][c].d[i] += fade * (tab[nxt][c].d[i] - tab[cur][c].d[i]);

    morphing = false;
    fade = 0.0f;
}

void WtVoice::synthInto (WtBuilder& builder, int slot)
{
    builder.synthesise (spec[0], harm, tab[slot][0]);
    if (specStereo)
        builder.synthesise (spec[1], harm, tab[slot][1]);
    slotStereo[slot] = specStereo;
}

void WtVoice::setTarget (WtBuilder& builder, const float* sliceL, const float* sliceR, int maxHarmonic, float morphSamples)
{
    bake();

    builder.analyse (sliceL, spec[0]);
    specStereo = (sliceR != nullptr);
    if (specStereo)
        builder.analyse (sliceR, spec[1]);

    harm = maxHarmonic;
    const int nxt = 1 - cur;
    synthInto (builder, nxt);

    if (! has || morphSamples < 2.0f)
    {
        cur = nxt;
        morphing = false;
    }
    else
    {
        morphing = true;
        fade = 0.0f;
        fadeInc = 1.0f / morphSamples;
    }
    has = true;
}

void WtVoice::relevel (WtBuilder& builder, int maxHarmonic, float morphSamples)
{
    if (! has)
        return;
    bake();

    harm = maxHarmonic;
    const int nxt = 1 - cur;
    synthInto (builder, nxt);

    if (morphSamples < 2.0f)
    {
        cur = nxt;
    }
    else
    {
        morphing = true;
        fade = 0.0f;
        fadeInc = 1.0f / morphSamples;
    }
}

void WtVoice::tick (float& l, float& r)
{
    if (! has)
    {
        l = r = 0.0f;
        return;
    }

    freq += (freqTarget - freq) * freqCoef;

    l = readTable (tab[cur][0], phase);
    r = slotStereo[cur] ? readTable (tab[cur][1], phase) : l;

    if (morphing)
    {
        const int nxt = 1 - cur;
        const float nl = readTable (tab[nxt][0], phase);
        const float nr = slotStereo[nxt] ? readTable (tab[nxt][1], phase) : nl;
        l += fade * (nl - l);
        r += fade * (nr - r);
        fade += fadeInc;
        if (fade >= 1.0f)
        {
            cur = nxt;
            morphing = false;
            fade = 0.0f;
        }
    }

    phase += freq / sr;
    phase -= std::floor (phase);
}

} // namespace ceo
