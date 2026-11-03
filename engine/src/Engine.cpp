#include "ceo/Engine.h"
#include "ceo/Mapping.h"
#include "ceo/Window.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ceo
{
namespace
{

// Small, fast random number generator (xorshift). Our own, no library needed.
struct Rng
{
    uint32_t state = 0x9E3779B9u;

    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    // 0.0 (inclusive) to 1.0 (exclusive)
    float uniform() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
};

// One playing grain. It keeps the settings it was started with.
struct Grain
{
    bool        active = false;
    double      pos    = 0.0;     // where in the layer we are reading (in samples)
    double      step   = 1.0;     // samples to move per output sample (negative = backwards)
    int         age    = 0;
    int         len    = 1;       // grain length in output samples
    float       gainL  = 0.0f;
    float       gainR  = 0.0f;
    GrainWindow window;
};

// Reads the layer at a fractional position using 4-point cubic interpolation
// (smooth when a grain is pitched). Anywhere outside the recording is silence.
inline float readCubic (const float* buf, int n, double pos)
{
    const int   i = (int) std::floor (pos);
    const float f = (float) (pos - (double) i);

    float y0, y1, y2, y3;
    if (i >= 1 && i + 2 < n)
    {
        y0 = buf[i - 1];
        y1 = buf[i];
        y2 = buf[i + 1];
        y3 = buf[i + 2];
    }
    else
    {
        auto at = [buf, n] (int k) { return (k >= 0 && k < n) ? buf[k] : 0.0f; };
        y0 = at (i - 1);
        y1 = at (i);
        y2 = at (i + 1);
        y3 = at (i + 2);
    }

    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

inline float sanitise (float x)
{
    return std::isfinite (x) ? x : 0.0f;
}

} // namespace

struct Engine::Impl
{
    // Layer alpha: 10 s plus the 3 s tail, one channel (stereo input is summed to mono).
    std::vector<float> layer;
    int  layerSamples = 0;          // the 10 s part (this is what Scan and Spray span)
    int  totalSamples = 0;          // 10 s + tail
    bool hasContent   = false;

    // Recording
    bool recording   = false;
    int  recHead     = 0;
    bool prevCapture = false;
    bool primed      = false;       // false until the first block after a reset

    // Smoothed level controls (ramped across each block so knob moves do not click)
    bool  gainsPrimed = false;
    float inG = 1.0f, dryG = 1.0f, wetG = 0.0f, outG = 1.0f;

    // Grains
    Grain  grains[kMaxGrains];
    int    activeGrains     = 0;
    int    activeContinuous = 0;
    double countdown        = 0.0;  // samples until the next continuous grain starts
    bool   wasContinuous    = false;
    Rng    rng;
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}

Engine::~Engine() = default;

void Engine::prepare (double newSampleRate, int newMaxBlockSize, int numChannels)
{
    sampleRate   = newSampleRate;
    maxBlockSize = newMaxBlockSize;
    channelCount = numChannels;

    const int newLayer = (int) std::lround ((double) kLayerSeconds * sampleRate);
    const int newTotal = newLayer + (int) std::lround ((double) kTailSeconds * sampleRate);

    // Only reallocate (and so lose the recording) if the sample rate changed.
    if (newTotal != impl->totalSamples)
    {
        impl->layer.assign ((size_t) newTotal, 0.0f);
        impl->layerSamples = newLayer;
        impl->totalSamples = newTotal;
        impl->hasContent   = false;
    }

    GrainWindow warmUp;                 // builds the shared window tables now, not on the audio thread
    warmUp.set (0.5f, 0.1f);

    reset();
}

void Engine::reset()
{
    Impl& s = *impl;
    s.recording        = false;
    s.recHead          = 0;
    s.primed           = false;
    s.gainsPrimed      = false;
    s.activeGrains     = 0;
    s.activeContinuous = 0;
    s.countdown        = 0.0;
    s.wasContinuous    = false;
    for (auto& g : s.grains)
        g.active = false;
}

void Engine::setParams (const Params& newParams)
{
    params = newParams;

    auto fix = [] (float& v) { v = std::isfinite (v) ? map::clamp01 (v) : 0.5f; };
    fix (params.inputLevel);     fix (params.dryWet);          fix (params.outputLevel);
    fix (params.intensity);      fix (params.grainWindow);     fix (params.grainDirection);
    fix (params.length);         fix (params.scan);            fix (params.spray);
    fix (params.pitch);          fix (params.pitchDeviation);  fix (params.modAmount);
    fix (params.hold);           fix (params.sense);           fix (params.dub);
    params.layer = std::max (0, std::min (6, params.layer));
}

bool Engine::isRecording() const     { return impl->recording; }
bool Engine::layerHasContent() const { return impl->hasContent; }
int  Engine::activeGrainCount() const { return impl->activeGrains; }

void Engine::process (float* const* channels, int numChannels, int numSamples)
{
    Impl& s = *impl;
    if (numChannels <= 0 || numSamples <= 0 || s.totalSamples <= 0)
        return;

    const int nCh = std::min (numChannels, 2);      // only the first two channels are used

    // ---- Capture button: starts or stops recording (checked once per block) ----
    if (! s.primed)
    {
        s.prevCapture = params.capture;             // a Capture switch that is already on at load does nothing
        s.primed = true;
    }

    if (params.capture && ! s.prevCapture)
    {
        // Start from the beginning of the layer. Only layer alpha exists in milestone 3.
        s.recording = (params.layer == 0);
        s.recHead   = 0;
    }
    else if (! params.capture && s.prevCapture)
    {
        s.recording = false;
    }
    s.prevCapture = params.capture;

    // ---- Settings that hold for the whole block ----
    const double sr         = sampleRate;
    const float  lenSeconds = map::grainLengthSeconds (params.length);
    const double lenSamples = std::max (8.0, (double) lenSeconds * sr);
    const auto   inten      = map::intensity (params.intensity);
    const float  revProb    = map::reverseProbability (params.grainDirection);
    const float  ratio      = map::pitchRatio (params.pitch);
    const double scanPos    = (double) params.scan * (double) s.layerSamples;
    const float  spray      = params.spray;
    const float  sprayCurve = 1.0f + 3.0f * (1.0f - spray) * (1.0f - spray);   // leans towards the Scan position, flat (fully random) at maximum

    const bool continuousOn = inten.active && params.layer == 0 && s.hasContent;

    const float targetIn  = map::inputGain (params.inputLevel);
    const float targetDry = map::dryGain (params.dryWet);
    const float targetWet = map::wetGain (params.dryWet);
    const float targetOut = map::outputGain (params.outputLevel);

    if (! s.gainsPrimed)
    {
        s.inG = targetIn;  s.dryG = targetDry;  s.wetG = targetWet;  s.outG = targetOut;
        s.gainsPrimed = true;
    }

    const float invN  = 1.0f / (float) numSamples;
    const float dIn   = (targetIn  - s.inG)  * invN;
    const float dDry  = (targetDry - s.dryG) * invN;
    const float dWet  = (targetWet - s.wetG) * invN;
    const float dOut  = (targetOut - s.outG) * invN;

    if (continuousOn && ! s.wasContinuous)
        s.countdown = 0.0;                          // start a grain straight away
    s.wasContinuous = continuousOn;

    const float* layerData = s.layer.data();

    // Starts one continuous grain.
    auto startGrain = [&]()
    {
        if (s.activeContinuous >= kMaxContinuousGrains)
            return;

        Grain* g = nullptr;
        for (auto& candidate : s.grains)
            if (! candidate.active) { g = &candidate; break; }
        if (g == nullptr)
            return;

        // Where it starts: Scan position plus a random Spray offset, wrapping around the layer end.
        const double offset = (double) s.layerSamples * (double) spray * std::pow (s.rng.uniform(), sprayCurve);
        const double start  = std::fmod (scanPos + offset, (double) s.layerSamples);

        const bool reversed = s.rng.uniform() < revProb;

        g->len  = (int) lenSamples;
        g->age  = 0;
        g->step = reversed ? -(double) ratio : (double) ratio;
        g->pos  = reversed ? start + (double) (g->len - 1) * (double) ratio : start;

        g->window.set (params.grainWindow, lenSeconds);

        // Random position between left and right (equal power).
        const float angle = s.rng.uniform() * 0.5f * map::kPi;
        // Random-level grains (Intensity above centre) vary between 25% and 100%; the 1.5 brings their
        // average loudness back up to match the even side.
        const float amp   = inten.sync ? 1.0f : 1.5f * (0.25f + 0.75f * s.rng.uniform());
        const float base  = amp * g->window.loudnessCompensation() / std::sqrt (std::max (1.0f, inten.grains));
        g->gainL = base * std::cos (angle);
        g->gainR = base * std::sin (angle);

        g->active = true;
        ++s.activeGrains;
        ++s.activeContinuous;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        s.inG += dIn;  s.dryG += dDry;  s.wetG += dWet;  s.outG += dOut;

        const float in0 = sanitise (channels[0][i]);
        const float in1 = nCh > 1 ? sanitise (channels[1][i]) : in0;

        // ---- Record into layer alpha ----
        if (s.recording)
        {
            const float mono = nCh > 1 ? 0.5f * (in0 + in1) : in0;
            s.layer[(size_t) s.recHead++] = map::softSat (mono * s.inG);
            s.hasContent = true;
            if (s.recHead >= s.totalSamples)
                s.recording = false;                // layer full
        }

        // ---- Start new continuous grains when it is time ----
        if (continuousOn)
        {
            s.countdown -= 1.0;
            for (int guard = 0; s.countdown <= 0.0 && guard < 8; ++guard)
            {
                startGrain();

                double interval = lenSamples / (double) inten.grains;       // evenly timed...
                if (! inten.sync)
                    interval *= 0.25 + 1.5 * (double) s.rng.uniform();     // ...or random
                s.countdown += std::max (1.0, interval);
            }
            if (s.countdown <= 0.0)
                s.countdown = 1.0;
        }

        // ---- Play all sounding grains ----
        float wetL = 0.0f, wetR = 0.0f;
        if (s.activeGrains > 0)
        {
            for (auto& g : s.grains)
            {
                if (! g.active)
                    continue;

                const float t = ((float) g.age + 0.5f) / (float) g.len;
                const float v = readCubic (layerData, s.totalSamples, g.pos) * g.window.at (t);
                wetL += v * g.gainL;
                wetR += v * g.gainR;

                g.pos += g.step;
                if (++g.age >= g.len)
                {
                    g.active = false;
                    --s.activeGrains;
                    --s.activeContinuous;
                }
            }
        }

        // Bring the grain bus up to roughly the input's loudness (random left/right placement
        // shares each grain between two channels), then bend (not chop) any peaks.
        wetL = map::softSat (wetL * 1.2f);
        wetR = map::softSat (wetR * 1.2f);

        // ---- Mix and output ----
        if (nCh == 1)
        {
            channels[0][i] = (s.dryG * in0 + s.wetG * 0.70710678f * (wetL + wetR)) * s.outG;
        }
        else
        {
            channels[0][i] = (s.dryG * in0 + s.wetG * wetL) * s.outG;
            channels[1][i] = (s.dryG * in1 + s.wetG * wetR) * s.outG;
        }
    }

    s.inG = targetIn;  s.dryG = targetDry;  s.wetG = targetWet;  s.outG = targetOut;
}

} // namespace ceo
