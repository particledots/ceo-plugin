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

// One playing grain. It keeps the settings it was started with, including
// which layer it reads from.
struct Grain
{
    bool         active = false;
    bool         strike = false;     // true = belongs to the Strike engine
    const float* data   = nullptr;   // the layer this grain reads (set when it starts)
    double       pos    = 0.0;       // where in the layer we are reading (in samples)
    double       step   = 1.0;       // samples to move per output sample (negative = backwards)
    int          age    = 0;
    int          len    = 1;         // grain length in output samples
    float        gainL  = 0.0f;
    float        gainR  = 0.0f;
    GrainWindow  window;
};

// Reads a layer at a fractional position using 4-point cubic interpolation
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
    // Six layers, one channel each (stereo input is summed to mono). Each is 10 s plus the 3 s tail.
    std::vector<float> layers;                  // kNumLayers * totalSamples
    int  layerSamples = 0;                      // the 10 s part (this is what Scan and Spray span)
    int  totalSamples = 0;                      // 10 s + tail
    bool has[kNumLayers] = { false, false, false, false, false, false };

    // One-step Undo: a copy of one layer as it was before it was last recorded over.
    std::vector<float> undoCopy;                // totalSamples
    int  undoLayerIndex = -1;

    // Recording
    bool recording   = false;
    int  recLayer    = 0;
    int  recHead     = 0;

    // Button states from the previous block (to spot clicks)
    bool prevCapture = false, prevStrike = false, prevErase = false, prevUndo = false, prevFollow = false;
    bool primed      = false;                   // false until the first block after a reset

    // Follow mode playhead (in samples from the start of the layer, or of alpha in Omega)
    double playhead  = 0.0;

    // Smoothed level controls (ramped across each block so knob moves do not click)
    bool  gainsPrimed = false;
    float inG = 1.0f, dryG = 1.0f, wetG = 0.0f, outG = 1.0f;

    // Grains
    Grain  grains[kMaxGrains];
    int    activeGrains     = 0;
    int    activeContinuous = 0;
    int    activeStrike     = 0;
    double countdown        = 0.0;              // samples until the next continuous grain starts
    bool   wasContinuous    = false;
    Rng    rng;

    float* layerData (int index) { return layers.data() + (size_t) index * (size_t) totalSamples; }
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

    // Only reallocate (and so lose the recordings) if the sample rate changed.
    if (newTotal != impl->totalSamples)
    {
        impl->layers.assign ((size_t) newTotal * (size_t) kNumLayers, 0.0f);
        impl->undoCopy.assign ((size_t) newTotal, 0.0f);
        impl->layerSamples   = newLayer;
        impl->totalSamples   = newTotal;
        impl->undoLayerIndex = -1;
        for (bool& h : impl->has)
            h = false;
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
    s.playhead         = 0.0;
    s.activeGrains     = 0;
    s.activeContinuous = 0;
    s.activeStrike     = 0;
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

bool Engine::isRecording() const      { return impl->recording; }
int  Engine::recordingLayer() const   { return impl->recording ? impl->recLayer : -1; }
int  Engine::undoLayer() const        { return impl->undoLayerIndex; }
int  Engine::activeGrainCount() const { return impl->activeGrains; }
int  Engine::activeStrikeGrainCount() const { return impl->activeStrike; }

bool Engine::layerHasContent() const
{
    for (bool h : impl->has)
        if (h)
            return true;
    return false;
}

bool Engine::layerHasContent (int layer) const
{
    return layer >= 0 && layer < kNumLayers && impl->has[layer];
}

double Engine::playheadSeconds() const
{
    return sampleRate > 0.0 ? impl->playhead / sampleRate : 0.0;
}

void Engine::process (float* const* channels, int numChannels, int numSamples)
{
    Impl& s = *impl;
    if (numChannels <= 0 || numSamples <= 0 || s.totalSamples <= 0)
        return;

    const int nCh = std::min (numChannels, 2);      // only the first two channels are used

    // ---- Layout of the layers along Scan (or along the Follow playhead) ----
    const bool   omega = (params.layer == 6);
    const double span  = (double) (omega ? kNumLayers : 1) * (double) s.layerSamples;

    if (s.playhead >= span || s.playhead < 0.0)     // the layer dial may have changed since last block
    {
        s.playhead = std::fmod (s.playhead, span);
        if (s.playhead < 0.0)
            s.playhead += span;
    }

    // The layer that Capture, Erase and Undo act on: the dial's layer, or in Omega the layer under the cursor.
    auto currentLayer = [&]() -> int
    {
        if (! omega)
            return params.layer;
        const double cursor = params.follow ? s.playhead : (double) params.scan * span;
        return std::max (0, std::min (kNumLayers - 1, (int) std::floor (cursor / (double) s.layerSamples)));
    };

    // ---- Buttons (checked once per block) ----
    if (! s.primed)
    {
        // A switch that is already on when the plugin starts does nothing.
        s.prevCapture = params.capture;
        s.prevStrike  = params.strike;
        s.prevErase   = params.erase;
        s.prevUndo    = params.undo;
        s.prevFollow  = params.follow;
        s.primed = true;
    }

    // Follow mode switched on: playhead starts from the beginning.
    if (params.follow && ! s.prevFollow)
        s.playhead = 0.0;

    // Capture: on = start recording from the start of the layer, off = stop.
    if (params.capture && ! s.prevCapture)
    {
        const int dest = currentLayer();
        if (s.has[dest])
        {
            // Recording over something: keep a copy so Undo can bring it back (one step).
            std::copy (s.layerData (dest), s.layerData (dest) + s.totalSamples, s.undoCopy.begin());
            s.undoLayerIndex = dest;
        }
        s.recording = true;
        s.recLayer  = dest;
        s.recHead   = 0;
        if (params.follow)
            s.playhead = 0.0;                       // playback restarts together with recording
    }
    else if (! params.capture && s.prevCapture)
    {
        s.recording = false;
    }

    // Erase and Undo: every click counts. Ignored while recording.
    if (params.erase != s.prevErase && ! s.recording)
    {
        const int target = currentLayer();
        float* d = s.layerData (target);
        std::fill (d, d + s.totalSamples, 0.0f);
        s.has[target] = false;
        if (s.undoLayerIndex == target)
            s.undoLayerIndex = -1;
    }

    if (params.undo != s.prevUndo && ! s.recording)
    {
        const int target = currentLayer();
        if (s.undoLayerIndex == target)
        {
            std::copy (s.undoCopy.begin(), s.undoCopy.end(), s.layerData (target));
            s.has[target] = true;
            s.undoLayerIndex = -1;
        }
    }

    // Strike: every click fires one grain. In Follow mode it restarts the playhead instead.
    bool strikePending = false;
    if (params.strike != s.prevStrike)
    {
        if (params.follow)
            s.playhead = 0.0;
        else
            strikePending = true;
    }

    s.prevCapture = params.capture;
    s.prevStrike  = params.strike;
    s.prevErase   = params.erase;
    s.prevUndo    = params.undo;
    s.prevFollow  = params.follow;

    // ---- Settings that hold for the whole block ----
    const double sr         = sampleRate;
    const float  lenSeconds = map::grainLengthSeconds (params.length);
    const double lenSamples = std::max (8.0, (double) lenSeconds * sr);
    const auto   inten      = map::intensity (params.intensity);
    const float  revProb    = map::reverseProbability (params.grainDirection);
    const float  baseRatio  = map::pitchRatio (params.pitch);
    const double scanPos    = (double) params.scan * span;
    const double followStep = params.follow ? (double) map::followSpeed (params.scan) : 0.0;
    const float  spray      = params.spray;
    const float  sprayCurve = 1.0f + 3.0f * (1.0f - spray) * (1.0f - spray);   // leans towards the Scan position, flat (fully random) at maximum
    const float  dubG       = map::dubGain (params.dub);

    bool contentAvailable = false;
    if (omega)
        contentAvailable = s.has[0] || s.has[1] || s.has[2] || s.has[3] || s.has[4] || s.has[5];
    else
        contentAvailable = s.has[params.layer];

    const bool continuousOn = inten.active && contentAvailable;

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

    // Starts one grain, from the continuous engine or the Strike engine.
    auto startGrain = [&] (bool strike)
    {
        if (strike ? (s.activeStrike >= kMaxStrikeGrains) : (s.activeContinuous >= kMaxContinuousGrains))
            return;

        Grain* g = nullptr;
        for (auto& candidate : s.grains)
            if (! candidate.active) { g = &candidate; break; }
        if (g == nullptr)
            return;

        // Where it starts, along the whole span (one layer, or all six in Omega).
        double startAll;
        if (params.follow)
        {
            // Follow: the start is spread over audio *before* the playhead, so it grows from nothing as the playhead moves on.
            const double window = std::min ((double) spray * span, s.playhead);
            startAll = s.playhead - window * (double) std::pow (s.rng.uniform(), sprayCurve);
        }
        else
        {
            // Scan: the Scan position plus a random Spray offset, wrapping around the end.
            const double offset = span * (double) spray * (double) std::pow (s.rng.uniform(), sprayCurve);
            startAll = std::fmod (scanPos + offset, span);
        }
        startAll = std::max (0.0, std::min (startAll, span - 1.0));

        int    layerIndex = params.layer;
        double start      = startAll;
        if (omega)
        {
            layerIndex = std::max (0, std::min (kNumLayers - 1, (int) std::floor (startAll / (double) s.layerSamples)));
            start      = startAll - (double) layerIndex * (double) s.layerSamples;
        }

        // Pitch: knob plus a random deviation, both fixed for the life of the grain.
        const float deviation = map::pitchDeviationSemitones (params.pitchDeviation, s.rng.uniform());
        const float ratio     = std::max (0.125f, std::min (8.0f, baseRatio * std::exp2 (deviation / 12.0f)));

        const bool reversed = s.rng.uniform() < revProb;

        // Random position between left and right (equal power).
        const float angle = s.rng.uniform() * 0.5f * map::kPi;
        // Random-level grains (Intensity above centre) vary between 25% and 100%; the 1.5 brings their
        // average loudness back up to match the even side.
        const float randomLevel = 1.5f * (0.25f + 0.75f * s.rng.uniform());

        if (! s.has[layerIndex])
            return;                                 // that part of Omega is empty: silence

        g->strike = strike;
        g->data   = s.layerData (layerIndex);
        g->len    = (int) lenSamples;
        g->age    = 0;
        g->step   = reversed ? -(double) ratio : (double) ratio;
        g->pos    = reversed ? start + (double) (g->len - 1) * (double) ratio : start;

        g->window.set (params.grainWindow, lenSeconds);

        const float amp   = strike ? map::kStrikeGain : (inten.sync ? 1.0f : randomLevel);
        const float base  = amp * g->window.loudnessCompensation() / std::sqrt (std::max (1.0f, inten.grains));
        g->gainL = base * std::cos (angle);
        g->gainR = base * std::sin (angle);

        g->active = true;
        ++s.activeGrains;
        if (strike) ++s.activeStrike; else ++s.activeContinuous;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        s.inG += dIn;  s.dryG += dDry;  s.wetG += dWet;  s.outG += dOut;

        const float in0 = sanitise (channels[0][i]);
        const float in1 = nCh > 1 ? sanitise (channels[1][i]) : in0;

        // ---- Follow mode playhead ----
        if (params.follow)
        {
            s.playhead += followStep;
            if (s.playhead >= span)
                s.playhead = std::fmod (s.playhead, span);
            else if (s.playhead < 0.0)
                s.playhead = std::fmod (s.playhead, span) + span;
        }

        // ---- Record into the chosen layer (new audio plus the old audio at the Dub level) ----
        if (s.recording)
        {
            float* slot = s.layerData (s.recLayer) + s.recHead++;
            const float mono = nCh > 1 ? 0.5f * (in0 + in1) : in0;
            *slot = map::softSat (mono * s.inG + *slot * dubG);
            s.has[s.recLayer] = true;
            if (s.recHead >= s.totalSamples)
                s.recording = false;                // layer full
        }

        // ---- Strike click ----
        if (strikePending)
        {
            strikePending = false;
            startGrain (true);
        }

        // ---- Start new continuous grains when it is time ----
        if (continuousOn)
        {
            s.countdown -= 1.0;
            for (int guard = 0; s.countdown <= 0.0 && guard < 8; ++guard)
            {
                startGrain (false);

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
                const float v = readCubic (g.data, s.totalSamples, g.pos) * g.window.at (t);
                wetL += v * g.gainL;
                wetR += v * g.gainR;

                g.pos += g.step;
                if (++g.age >= g.len)
                {
                    g.active = false;
                    --s.activeGrains;
                    if (g.strike) --s.activeStrike; else --s.activeContinuous;
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
