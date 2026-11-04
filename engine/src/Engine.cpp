#include "ceo/Engine.h"
#include "ceo/Effects.h"
#include "ceo/Mapping.h"
#include "ceo/Scales.h"
#include "ceo/OnsetDetector.h"
#include "ceo/Wavetable.h"
#include "ceo/Window.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
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
    const float* dataL  = nullptr;   // the layer this grain reads (set when it starts)
    const float* dataR  = nullptr;   // second channel of a stereo layer, or nullptr for a mono layer
    double       pos    = 0.0;       // where in the layer we are reading (in samples)
    double       step   = 1.0;       // samples to move per output sample (negative = backwards)
    int          age    = 0;
    int          len    = 1;         // grain length in output samples
    float        gainL  = 0.0f;
    float        gainR  = 0.0f;
    float        monoBlend = 0.0f;   // 0 = keep the layer's stereo image, 1 = fold to mono
    GrainWindow  window;
};

// A Strike note in Wavetable mode.
struct WtNote
{
    bool        active = false;
    WtVoice     voice;
    GrainWindow window;
    int         age = 0;
    int         len = 1;
    float       gainL = 0.0f;
    float       gainR = 0.0f;
    float       monoBlend = 0.0f;
};

constexpr float kWtGain         = 0.8f;    // oscillator level compared with grains
constexpr float kWtNoteSeconds  = 0.4f;    // length of a Strike note
constexpr float kWtEventSeconds = 0.35f;   // how often the continuous oscillator picks a new pitch deviation / place

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
    // Six layers. Each has a left plane and a right plane of 10 s plus the 3 s tail. A layer recorded in
    // Mono input mode only uses the left plane (layerStereo false); one recorded in Stereo mode uses both.
    std::vector<float> planes;                  // kNumLayers * 2 * totalSamples
    int  layerSamples = 0;                      // the 10 s part (this is what Scan and Spray span)
    int  totalSamples = 0;                      // 10 s + tail
    bool has[kNumLayers]         = { false, false, false, false, false, false };
    bool layerStereo[kNumLayers] = { false, false, false, false, false, false };
    std::atomic<uint32_t> rev { 0 };            // goes up whenever recorded audio changes (the oscillator and the display watch it)
    std::atomic<uint32_t> captureRanOut { 0 };  // goes up each time a Capture-button recording ends by itself (layer full)

    // For the stream display (read from the plugin window's thread while audio runs).
    static constexpr uint32_t kGrainRing = 128; // must be a power of two
    std::atomic<uint32_t> grainRing[kGrainRing] = {};
    std::atomic<uint32_t> grainStartCount { 0 };
    std::atomic<int>  displayReaders { 0 };     // window-thread readers currently looking at the layer memory
    std::atomic<bool> reallocating   { false }; // true while prepare() is replacing the layer memory

    void pushGrainStart (double position, bool strike, bool reversed)
    {
        const double clamped = std::max (0.0, std::min (1.0, position));
        const uint32_t packed = (uint32_t) std::lround (clamped * 65535.0) | (strike ? 0x10000u : 0u) | (reversed ? 0x20000u : 0u);
        const uint32_t n = grainStartCount.load (std::memory_order_relaxed);
        grainRing[n & (kGrainRing - 1)].store (packed, std::memory_order_relaxed);
        grainStartCount.store (n + 1, std::memory_order_release);
    }

    // One-step Undo: a copy of one layer as it was before it was last recorded over.
    std::vector<float> undoPlanes;              // 2 * totalSamples
    bool undoStereo      = false;
    int  undoLayerIndex  = -1;

    // Recording
    bool recording    = false;
    int  recLayer     = 0;
    int  recHead      = 0;
    int  recLimit     = 0;                      // recording stops by itself when recHead reaches this
    bool recStereo    = false;                  // input mode when this recording started
    bool recFromOnset = false;

    // Onset detection
    OnsetDetector detector;
    std::vector<float> ring;                    // the last moments of (mono) input, for the pre-roll
    int    ringPos      = 0;
    int    preRoll      = 0;
    double holdLeft     = 0.0;                  // samples until the detector is armed again
    bool   prevOnsetOn  = false;
    int    onsetCounter = 0;

    // Button states from the previous block (to spot clicks)
    bool prevCapture = false, prevStrike = false, prevErase = false, prevUndo = false, prevFollow = false;
    bool primed      = false;                   // false until the first block after a reset
    int  fadeLeft    = 0;                       // samples of the short fade-in after a soft restart
    int  fadeTotal   = 1;
    bool prepared    = false;                   // true once prepare() has done the full set-up at least once

    // Follow mode playhead (in samples from the start of the layer, or of alpha in Omega)
    double playhead  = 0.0;

    // Smoothed level controls (ramped across each block so knob moves do not click)
    bool  gainsPrimed = false;
    float inG = 1.0f, senseG = 1.0f, dryG = 1.0f, wetG = 0.0f, outG = 1.0f;
    float gGrain = 1.0f, gWt = 0.0f;            // crossfade between grains and the oscillator

    // Grains
    Grain  grains[kMaxGrains];
    int    activeGrains     = 0;
    int    activeContinuous = 0;
    int    activeStrike     = 0;
    double countdown        = 0.0;              // samples until the next continuous grain starts
    bool   wasContinuous    = false;
    Rng    rng;

    // Wavetable mode
    WtBuilder builder;
    WtVoice   osc;                              // the continuous oscillator
    WtNote    notes[kMaxWtNotes];
    int       activeNotes   = 0;
    bool      wtWasOn       = false;
    bool      wtInstant     = true;
    double    wtCountdown   = 0.0;
    float     wtU           = 0.0f;             // random number behind the current Spray offset
    float     wtDevRatio    = 1.0f;             // current random pitch deviation (as a ratio)
    float     wtMonoBlend   = 0.0f;
    float     wtPanL = 0.7071f, wtPanR = 0.7071f, wtPanTargetL = 0.7071f, wtPanTargetR = 0.7071f;
    float     wtLevel       = 0.0f;
    double    wtBuiltAt     = -1.0;
    uint32_t  wtBuiltRev    = 0;
    int       wtSinceBuild  = 0;

    // Effects (Mod section)
    Reverb      reverb;
    StereoDelay delay;
    float revDry = 1.0f, revWet = 0.0f, dlyLevel = 0.0f;
    bool  revRunning = false, dlyRunning = false;

    float* plane (int layer, int channel) { return planes.data() + ((size_t) layer * 2 + (size_t) channel) * (size_t) totalSamples; }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}

Engine::~Engine() = default;

void Engine::prepare (double newSampleRate, int newMaxBlockSize, int numChannels)
{
    const bool sameSetup = impl->prepared && newSampleRate == sampleRate && numChannels == channelCount;

    sampleRate   = newSampleRate;
    maxBlockSize = newMaxBlockSize;
    channelCount = numChannels;

    if (sameSetup)
    {
        // The same settings again (the host does this when playback restarts): carry on gently. Grains, the
        // oscillator and the reverb and echo tails keep going, so nothing is cut off with a click. Only what
        // belonged to the old playback position is dropped: recording stops, the Follow playhead goes back to
        // the start, the onset detector starts afresh, and the buttons are read again without counting as clicks.
        // (reset() is the hard version that silences everything.)
        Impl& s = *impl;
        s.recording    = false;
        s.recHead      = 0;
        s.primed       = false;
        s.playhead     = 0.0;
        s.detector.reset();
        std::fill (s.ring.begin(), s.ring.end(), 0.0f);
        s.ringPos      = 0;
        s.holdLeft     = 0.0;
        s.prevOnsetOn  = false;
        // The host's output was silent while stopped, so what carries on would start with a step from zero:
        // fade it in over 15 ms (the sound that keeps playing is otherwise untouched).
        s.fadeTotal    = std::max (1, (int) std::lround (0.015 * sampleRate));
        s.fadeLeft     = s.fadeTotal;
        return;
    }

    const int newLayer = (int) std::lround ((double) kLayerSeconds * sampleRate);
    const int newTotal = newLayer + (int) std::lround ((double) kTailSeconds * sampleRate);

    // Only reallocate (and so lose the recordings) if the sample rate changed.
    if (newTotal != impl->totalSamples)
    {
        // The plugin window may be reading the layers for the display: wait for it to finish, and keep it out meanwhile.
        impl->reallocating.store (true);
        while (impl->displayReaders.load() > 0)
            std::this_thread::yield();

        impl->planes.assign ((size_t) newTotal * 2 * (size_t) kNumLayers, 0.0f);
        impl->undoPlanes.assign ((size_t) newTotal * 2, 0.0f);
        impl->layerSamples   = newLayer;
        impl->totalSamples   = newTotal;
        impl->undoLayerIndex = -1;
        impl->undoStereo     = false;
        for (bool& h : impl->has)
            h = false;
        for (bool& st : impl->layerStereo)
            st = false;

        impl->reallocating.store (false);
    }

    impl->detector.prepare (sampleRate);
    impl->preRoll = (int) std::lround ((double) kPreRollSeconds * sampleRate);
    impl->ring.assign ((size_t) std::max (8192, 4 * impl->preRoll), 0.0f);
    impl->reverb.prepare (sampleRate);
    impl->delay.prepare (sampleRate);
    impl->osc.prepare (sampleRate);
    for (auto& n : impl->notes)
        n.voice.prepare (sampleRate);

    GrainWindow warmUp;                 // builds the shared window tables now, not on the audio thread
    warmUp.set (0.5f, 0.1f);

    impl->prepared = true;
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

    s.detector.reset();
    std::fill (s.ring.begin(), s.ring.end(), 0.0f);
    s.ringPos     = 0;
    s.holdLeft    = 0.0;
    s.prevOnsetOn = false;

    s.osc.clear();
    for (auto& n : s.notes)
    {
        n.active = false;
        n.voice.clear();
    }
    s.activeNotes = 0;
    s.wtWasOn     = false;
    s.wtInstant   = true;
    s.wtCountdown = 0.0;
    s.wtLevel     = 0.0f;
    s.wtBuiltAt   = -1.0;
    s.wtSinceBuild = 0;

    s.reverb.reset();
    s.delay.reset();
    s.revDry = 1.0f;  s.revWet = 0.0f;  s.dlyLevel = 0.0f;
    s.revRunning = s.dlyRunning = false;
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
    params.layer     = std::max (0, std::min (6, params.layer));
    params.modTarget = std::max (0, std::min (3, params.modTarget));
    params.pitchSnap  = std::max (0, std::min (2, params.pitchSnap));
    params.pitchScale = std::max (0, std::min (scales::kNumScales, params.pitchScale));
}

bool Engine::isRecording() const      { return impl->recording; }
int  Engine::recordingLayer() const   { return impl->recording ? impl->recLayer : -1; }
int  Engine::undoLayer() const        { return impl->undoLayerIndex; }
int  Engine::activeGrainCount() const { return impl->activeGrains; }
int  Engine::activeStrikeGrainCount() const { return impl->activeStrike; }
int  Engine::onsetCount() const       { return impl->onsetCounter; }
bool Engine::onsetArmed() const       { return impl->holdLeft <= 0.0; }
bool Engine::wavetableActive() const  { return impl->activeNotes > 0 || impl->wtLevel > 1e-3f; }

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

bool Engine::layerIsStereo (int layer) const
{
    return layer >= 0 && layer < kNumLayers && impl->has[layer] && impl->layerStereo[layer];
}

const float* Engine::layerAudio (int layer, int channel) const
{
    if (layer < 0 || layer >= kNumLayers || channel < 0 || channel > 1 || impl->totalSamples <= 0)
        return nullptr;
    return impl->planes.data() + ((size_t) layer * 2 + (size_t) channel) * (size_t) impl->totalSamples;
}

int Engine::layerAudioLength() const
{
    return impl->totalSamples;
}

double Engine::playheadSeconds() const
{
    return sampleRate > 0.0 ? impl->playhead / sampleRate : 0.0;
}

uint32_t Engine::recordingRevision() const
{
    return impl->rev.load();
}

uint32_t Engine::captureRanOutCount() const
{
    return impl->captureRanOut.load();
}

int Engine::takeGrainStarts (uint32_t& cursor, GrainStart* out, int maxCount) const
{
    if (out == nullptr || maxCount <= 0)
        return 0;

    const Impl& s = *impl;
    const uint32_t end = s.grainStartCount.load (std::memory_order_acquire);

    uint32_t available = end - cursor;                  // wraps correctly for an ordinary cursor
    if (available > Impl::kGrainRing)
        available = Impl::kGrainRing;                   // the older ones have been overwritten
    if (available > (uint32_t) maxCount)
        available = (uint32_t) maxCount;

    const uint32_t first = end - available;
    for (uint32_t i = 0; i < available; ++i)
    {
        const uint32_t packed = s.grainRing[(first + i) & (Impl::kGrainRing - 1)].load (std::memory_order_relaxed);
        out[i].position = (float) (packed & 0xFFFFu) / 65535.0f;
        out[i].strike   = (packed & 0x10000u) != 0;
        out[i].reversed = (packed & 0x20000u) != 0;
    }

    cursor = end;
    return (int) available;
}

bool Engine::layerEnvelope (int layer, float* out, int points) const
{
    if (out == nullptr || points <= 0 || layer < 0 || layer >= kNumLayers)
        return false;

    Impl& s = *impl;

    // Keep prepare() from replacing the memory while we look at it.
    s.displayReaders.fetch_add (1);
    if (s.reallocating.load())
    {
        s.displayReaders.fetch_sub (1);
        return false;
    }

    const int n = s.layerSamples;
    if (n <= 0 || s.totalSamples <= 0 || ! s.has[layer])
    {
        for (int i = 0; i < points; ++i)
            out[i] = 0.0f;
        s.displayReaders.fetch_sub (1);
        return true;
    }

    const float* left  = s.planes.data() + ((size_t) layer * 2) * (size_t) s.totalSamples;
    const float* right = s.layerStereo[layer] ? left + s.totalSamples : nullptr;

    // Every 13th sample is plenty for a loudness shape, and 13 does not line up with common pitches the way a round number could.
    constexpr int kStride = 13;
    for (int p = 0; p < points; ++p)
    {
        const int from = (int) ((int64_t) n * p / points);
        const int to   = (int) ((int64_t) n * (p + 1) / points);
        double sum = 0.0;
        int    count = 0;
        for (int i = from; i < to; i += kStride)
        {
            double v2 = (double) left[i] * (double) left[i];
            if (right != nullptr)
                v2 = 0.5 * (v2 + (double) right[i] * (double) right[i]);
            sum += v2;
            ++count;
        }
        out[p] = count > 0 ? (float) std::sqrt (sum / (double) count) : 0.0f;
    }

    s.displayReaders.fetch_sub (1);
    return true;
}

void Engine::process (float* const* channels, int numChannels, int numSamples)
{
    Impl& s = *impl;
    if (numChannels <= 0 || numSamples <= 0 || s.totalSamples <= 0)
        return;

    const int  nCh        = std::min (numChannels, 2);      // only the first two channels are used
    const bool stereoMode = params.stereoInput && nCh > 1;  // Stereo input mode needs two channels

    // ---- Mod section: what the Mod amount does depends on the target ----
    const float modD      = map::modBipolar (params.modAmount);
    const float modPan    = params.modTarget == kModPan    ? modD : 0.0f;
    const float modHold   = params.modTarget == kModHold   ? modD : 0.0f;
    const float modReverb = params.modTarget == kModReverb ? modD : 0.0f;
    const float modDelay  = params.modTarget == kModDelay  ? modD : 0.0f;

    const float  holdKnob    = map::holdKnobWithMod (params.hold, modHold);
    const double holdSeconds = (double) map::holdSeconds (holdKnob);

    // ---- Layout of the layers along Scan (or along the Follow playhead) ----
    const bool   omega = (params.layer == 6);
    const double span  = (double) (omega ? kNumLayers : 1) * (double) s.layerSamples;

    // In Follow mode the playhead loops over a stretch as long as Hold (in Omega, six times as long,
    // so the top of the Hold knob is still the whole thing).
    const double loopLen = params.follow
                               ? std::max (8.0, std::min (span, holdSeconds * sampleRate * (double) (omega ? kNumLayers : 1)))
                               : span;

    if (s.playhead >= loopLen || s.playhead < 0.0)  // the layer dial or Hold may have changed since last block
    {
        s.playhead = std::fmod (s.playhead, loopLen);
        if (s.playhead < 0.0)
            s.playhead += loopLen;
    }

    // The layer that Capture, Erase and Undo act on: the dial's layer, or in Omega the layer under the cursor.
    auto currentLayer = [&]() -> int
    {
        if (! omega)
            return params.layer;
        const double cursor = params.follow ? s.playhead : (double) params.scan * span;
        return std::max (0, std::min (kNumLayers - 1, (int) std::floor (cursor / (double) s.layerSamples)));
    };

    const float dubG = map::dubGain (params.dub);

    // Writes one frame of new audio into the recording layer (new audio plus the old audio at the Dub level).
    auto recordFrame = [&] (int index, float newL, float newR)
    {
        float* left = s.plane (s.recLayer, 0);
        left[index] = map::softSat (newL + left[index] * dubG);
        if (s.layerStereo[s.recLayer])
        {
            float* right = s.plane (s.recLayer, 1);
            right[index] = map::softSat (newR + right[index] * dubG);
        }
    };

    // Starts recording into the layer Capture acts on (used by the Capture button and by onsets).
    auto beginRecording = [&] (bool fromOnset)
    {
        const int dest = currentLayer();
        if (s.has[dest])
        {
            // Recording over something: keep a copy so Undo can bring it back (one step).
            std::copy (s.plane (dest, 0), s.plane (dest, 0) + s.totalSamples, s.undoPlanes.begin());
            if (s.layerStereo[dest])
                std::copy (s.plane (dest, 1), s.plane (dest, 1) + s.totalSamples, s.undoPlanes.begin() + s.totalSamples);
            s.undoStereo     = s.layerStereo[dest];
            s.undoLayerIndex = dest;
        }

        // A layer that is mono so far becomes a stereo layer when recorded over in Stereo input mode;
        // what was in it is copied to the right side, so it keeps sounding the same.
        if (stereoMode && ! s.layerStereo[dest])
        {
            if (s.has[dest])
                std::copy (s.plane (dest, 0), s.plane (dest, 0) + s.totalSamples, s.plane (dest, 1));
            else
                std::fill (s.plane (dest, 1), s.plane (dest, 1) + s.totalSamples, 0.0f);
            s.layerStereo[dest] = true;
        }
        else if (! stereoMode && ! s.has[dest])
        {
            s.layerStereo[dest] = false;
        }

        s.recording    = true;
        s.recLayer     = dest;
        s.recHead      = 0;
        s.recLimit     = s.totalSamples;
        s.recStereo    = stereoMode;
        s.recFromOnset = fromOnset;
        if (params.follow)
            s.playhead = 0.0;                       // playback restarts together with recording
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
        beginRecording (false);
    }
    else if (! params.capture && s.prevCapture)
    {
        s.recording = false;
    }

    // Erase and Undo: every click counts. Ignored while recording.
    if (params.erase != s.prevErase && ! s.recording)
    {
        const int target = currentLayer();
        if (s.has[target])
        {
            // Keep a copy so Undo can bring the layer back (one step, same slot as record-over).
            std::copy (s.plane (target, 0), s.plane (target, 0) + s.totalSamples, s.undoPlanes.begin());
            if (s.layerStereo[target])
                std::copy (s.plane (target, 1), s.plane (target, 1) + s.totalSamples, s.undoPlanes.begin() + s.totalSamples);
            s.undoStereo     = s.layerStereo[target];
            s.undoLayerIndex = target;
        }
        float* d = s.plane (target, 0);
        std::fill (d, d + s.totalSamples, 0.0f);
        s.has[target]         = false;
        s.layerStereo[target] = false;
        ++s.rev;
    }

    if (params.undo != s.prevUndo && ! s.recording)
    {
        const int target = currentLayer();
        if (s.undoLayerIndex == target)
        {
            std::copy (s.undoPlanes.begin(), s.undoPlanes.begin() + s.totalSamples, s.plane (target, 0));
            if (s.undoStereo)
                std::copy (s.undoPlanes.begin() + s.totalSamples, s.undoPlanes.end(), s.plane (target, 1));
            s.layerStereo[target] = s.undoStereo;
            s.has[target]         = true;
            s.undoLayerIndex      = -1;
            ++s.rev;
        }
    }

    // Strike: every click fires one grain (or one note in Wavetable mode). In Follow mode it restarts the playhead instead.
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
    const float  baseRatio  = map::pitchRatioSnapped (params.pitch, params.pitchSnap);   // the Pitch knob's note, with Pitch Snap
    const int    scaleMask  = scales::maskFor (params.pitchScale);                           // Pitch Scale for the random jumps (0 = Off)
    const double scanPos    = (double) params.scan * span;
    const double followStep = params.follow ? (double) map::followSpeed (params.scan) : 0.0;
    const float  spray      = params.spray;
    const float  sprayCurve = 1.0f + 3.0f * (1.0f - spray) * (1.0f - spray);   // leans towards the Scan position, flat (fully random) at maximum

    // Wavetable mode: Length fully down crossfades from grains into the oscillator.
    const float wtMixTarget  = map::wavetableMix (params.length);
    const float gGrainTarget = std::cos (0.5f * map::kPi * wtMixTarget);
    const float gWtTarget    = std::sin (0.5f * map::kPi * wtMixTarget);
    const bool  wtOn         = wtMixTarget > 0.001f;
    const bool  grainsOn     = gGrainTarget > 0.001f;
    const float morphSamples = map::wavetableMorphSeconds (params.grainDirection) * (float) sr;

    bool contentAvailable = false;
    if (omega)
        contentAvailable = s.has[0] || s.has[1] || s.has[2] || s.has[3] || s.has[4] || s.has[5];
    else
        contentAvailable = s.has[params.layer];

    const bool continuousOn = inten.active && contentAvailable && grainsOn;
    const bool wtContOn     = inten.active && contentAvailable && wtOn;

    const float targetIn    = map::inputGain (params.inputLevel);
    const float targetSense = map::senseGain (params.sense);
    const float targetDry   = map::dryGain (params.dryWet);
    const float targetWet   = map::wetGain (params.dryWet);
    const float targetOut   = map::outputGain (params.outputLevel);

    if (! s.gainsPrimed)
    {
        s.inG = targetIn;  s.senseG = targetSense;  s.dryG = targetDry;  s.wetG = targetWet;  s.outG = targetOut;
        s.gGrain = gGrainTarget;  s.gWt = gWtTarget;
        s.gainsPrimed = true;
    }

    const float invN   = 1.0f / (float) numSamples;
    const float dIn    = (targetIn    - s.inG)    * invN;
    const float dSense = (targetSense - s.senseG) * invN;
    const float dDry   = (targetDry   - s.dryG)   * invN;
    const float dWet   = (targetWet   - s.wetG)   * invN;
    const float dOut   = (targetOut   - s.outG)   * invN;
    const float mixCoef   = 1.0f - std::exp (-1.0f / (0.003f * (float) sr));
    const float levelCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));
    const float panCoef   = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
    const float fxCoef    = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));

    if (continuousOn && ! s.wasContinuous)
        s.countdown = 0.0;                          // start a grain straight away
    s.wasContinuous = continuousOn;

    // ---- Effects: set up for this block ----
    const auto revMap = map::reverbMap (modReverb);
    const auto dlyMap = map::delayMap (modDelay);
    const float revDryTarget = revMap.active ? revMap.dry : 1.0f;
    const float revWetTarget = revMap.active ? revMap.wet : 0.0f;
    const float dlyTarget    = dlyMap.active ? dlyMap.level : 0.0f;
    if (revMap.active)
        s.reverb.set (revMap.decay, revMap.freeze, revMap.shimmer);
    if (dlyMap.active)
        s.delay.set (dlyMap.time, dlyMap.feedback, dlyMap.metallic ? 0.5f : 0.35f, dlyMap.inGain, dlyMap.metallic ? 1.0f : 1.11f);

    const bool revRun = revWetTarget > 0.0f || s.revWet > 1e-4f;
    const bool dlyRun = dlyTarget > 0.0f || s.dlyLevel > 1e-4f;
    if (! revRun && s.revRunning) { s.reverb.reset(); s.revRunning = false; s.revDry = 1.0f; s.revWet = 0.0f; }
    if (! dlyRun && s.dlyRunning) { s.delay.reset();  s.dlyRunning = false; s.dlyLevel = 0.0f; }

    // ---- Onset detection ----
    const bool   onsetOn  = params.onsetCapture && ! stereoMode;
    const auto   onsetMap = map::onsetSensitivity (params.sense);
    if (onsetOn && ! s.prevOnsetOn)
    {
        s.detector.reset();
        s.holdLeft = 0.0;
    }
    s.prevOnsetOn = onsetOn;

    // ---- Where grains and notes start ----
    // Position along the whole span (one layer, or all six in Omega) for a random number u.
    auto pickStartAll = [&] (float u) -> double
    {
        double startAll;
        if (params.follow)
        {
            // Follow: the start is spread over audio *before* the playhead, so it grows from nothing as the playhead moves on.
            const double window = std::min ((double) spray * span, s.playhead);
            startAll = s.playhead - window * (double) std::pow (u, sprayCurve);
        }
        else
        {
            // Scan: the Scan position plus a random Spray offset, wrapping around the end.
            const double offset = span * (double) spray * (double) std::pow (u, sprayCurve);
            startAll = std::fmod (scanPos + offset, span);
        }
        return std::max (0.0, std::min (startAll, span - 1.0));
    };

    auto resolveLayer = [&] (double startAll, int& layerIndex, double& start)
    {
        layerIndex = params.layer;
        start      = startAll;
        if (omega)
        {
            layerIndex = std::max (0, std::min (kNumLayers - 1, (int) std::floor (startAll / (double) s.layerSamples)));
            start      = startAll - (double) layerIndex * (double) s.layerSamples;
        }
    };

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

        int    layerIndex;
        double start;
        const double startAll = pickStartAll (s.rng.uniform());
        resolveLayer (startAll, layerIndex, start);

        // Pitch: knob plus a random deviation, both fixed for the life of the grain.
        const float deviation = map::pitchDeviationSemitones (params.pitchDeviation, s.rng.uniform(), scaleMask);
        const float ratio     = std::max (0.125f, std::min (8.0f, baseRatio * std::exp2 (deviation / 12.0f)));

        const bool reversed = s.rng.uniform() < revProb;

        // Position between left and right (equal power), shaped by the Pan setting of the Mod section.
        const auto pan = map::panSettings (modPan, s.rng.uniform(), s.rng.uniform());
        // Random-level grains (Intensity above centre) vary between 25% and 100%; the 1.5 brings their
        // average loudness back up to match the even side.
        const float randomLevel = 1.5f * (0.25f + 0.75f * s.rng.uniform());

        if (! s.has[layerIndex])
            return;                                 // that part of Omega is empty: silence

        g->strike    = strike;
        g->dataL     = s.plane (layerIndex, 0);
        g->dataR     = s.layerStereo[layerIndex] ? s.plane (layerIndex, 1) : nullptr;
        g->len       = (int) lenSamples;
        g->age       = 0;
        g->step      = reversed ? -(double) ratio : (double) ratio;
        g->pos       = reversed ? start + (double) (g->len - 1) * (double) ratio : start;
        g->monoBlend = pan.monoBlend;

        g->window.set (params.grainWindow, lenSeconds);

        const float amp   = strike ? map::kStrikeGain : (inten.sync ? 1.0f : randomLevel);
        // A Strike is an accent: it is not turned down for a dense texture, so it stands out of it.
        const float base  = amp * g->window.loudnessCompensation() / (strike ? 1.0f : std::sqrt (std::max (1.0f, inten.grains)));
        g->gainL = base * std::cos (pan.angle);
        g->gainR = base * std::sin (pan.angle);

        g->active = true;
        ++s.activeGrains;
        if (strike) ++s.activeStrike; else ++s.activeContinuous;

        s.pushGrainStart (startAll / span, strike, reversed);    // tells the stream display
    };

    // ---- Wavetable helpers ----
    auto wtFrequency = [&] () -> double
    {
        return (double) map::kWavetableCentreHz * (double) std::max (0.125f, std::min (8.0f, baseRatio * s.wtDevRatio));
    };

    // Cuts a slice for a table out of the layer at a start position. Returns false if that layer is empty.
    auto wtSlice = [&] (double startAll, const float*& left, const float*& right) -> bool
    {
        int    layerIndex;
        double start;
        resolveLayer (startAll, layerIndex, start);
        if (! s.has[layerIndex])
            return false;
        const int from = std::max (0, std::min ((int) start, s.totalSamples - kWtSize));
        left  = s.plane (layerIndex, 0) + from;
        right = s.layerStereo[layerIndex] ? s.plane (layerIndex, 1) + from : nullptr;
        return true;
    };

    // Builds a new table for the continuous oscillator from the current place in the layer.
    auto wtRetarget = [&] (float morph)
    {
        const double startAll = pickStartAll (s.wtU);
        s.wtBuiltAt    = startAll;
        s.wtBuiltRev   = s.rev;
        s.wtSinceBuild = 0;

        const float* left;
        const float* right;
        if (! wtSlice (startAll, left, right))
        {
            s.osc.clear();
            return;
        }
        s.osc.setTarget (s.builder, left, right, wtMaxHarmonic (wtFrequency(), sr), morph);
    };

    // One Strike note.
    auto startNote = [&] ()
    {
        WtNote* n = nullptr;
        for (auto& c : s.notes)
            if (! c.active) { n = &c; break; }
        if (n == nullptr)
            return;

        const float* left;
        const float* right;
        if (! wtSlice (pickStartAll (s.rng.uniform()), left, right))
            return;

        const float deviation = map::pitchDeviationSemitones (params.pitchDeviation, s.rng.uniform(), scaleMask);
        const float ratio     = std::max (0.125f, std::min (8.0f, baseRatio * std::exp2 (deviation / 12.0f)));
        const double hz       = (double) map::kWavetableCentreHz * (double) ratio;
        const auto pan        = map::panSettings (modPan, s.rng.uniform(), s.rng.uniform());

        n->voice.setFrequency (hz, true);
        n->voice.restartPhase();
        n->voice.setTarget (s.builder, left, right, wtMaxHarmonic (hz, sr), 0.0f);
        n->window.set (params.grainWindow, kWtNoteSeconds);
        n->len       = std::max (8, (int) (kWtNoteSeconds * sr));
        n->age       = 0;
        n->gainL     = map::kStrikeGain * std::cos (pan.angle);
        n->gainR     = map::kStrikeGain * std::sin (pan.angle);
        n->monoBlend = pan.monoBlend;
        n->active    = true;
        ++s.activeNotes;
    };

    // Housekeeping for the continuous oscillator, once per block.
    if (wtContOn)
    {
        if (! s.wtWasOn)
        {
            s.wtCountdown = 0.0;                    // pick a place and a pitch straight away
            s.wtInstant   = true;
        }
        else
        {
            const double hz = wtFrequency();
            s.osc.setFrequency (hz, false);
            if (s.osc.hasTable())
            {
                const int h = wtMaxHarmonic (hz, sr);
                if (h != s.osc.harmonics())
                    s.osc.relevel (s.builder, h, 0.02f * (float) sr);
            }

            // Follow the Scan / Spray knobs and any change in the recorded audio, about 40 times a second at most.
            s.wtSinceBuild += numSamples;
            if (s.wtSinceBuild >= (int) (0.025 * sr))
            {
                const double now = pickStartAll (s.wtU);
                if (! s.osc.hasTable() || s.rev != s.wtBuiltRev || std::fabs (now - s.wtBuiltAt) > 64.0)
                    wtRetarget (morphSamples);
                else
                    s.wtSinceBuild = 0;
            }
        }
    }
    s.wtWasOn = wtContOn;

    // ---- Onset-triggered recording ----
    auto onsetTrigger = [&] ()
    {
        beginRecording (true);
        const double holdSamples = holdSeconds * sr;
        s.holdLeft = holdSamples;
        s.recLimit = (int) std::min ((double) s.totalSamples, (double) s.preRoll + holdSamples);

        // Start the recording a little before the detection, so the attack itself is kept.
        const int ringSize = (int) s.ring.size();
        const int n = std::min (s.preRoll, s.recLimit);
        for (int j = 0; j < n; ++j)
        {
            int p = s.ringPos - 1 - n + j;
            while (p < 0)
                p += ringSize;
            const float v = s.ring[(size_t) (p % ringSize)] * s.inG;
            recordFrame (j, v, v);
        }
        s.recHead = n;
        s.has[s.recLayer] = true;
        ++s.onsetCounter;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        s.inG += dIn;  s.senseG += dSense;  s.dryG += dDry;  s.wetG += dWet;  s.outG += dOut;
        s.gGrain += (gGrainTarget - s.gGrain) * mixCoef;
        s.gWt    += (gWtTarget    - s.gWt)    * mixCoef;

        const float in0 = sanitise (channels[0][i]);
        const float in1 = nCh > 1 ? sanitise (channels[1][i]) : in0;

        // ---- Follow mode playhead ----
        if (params.follow)
        {
            s.playhead += followStep;
            if (s.playhead >= loopLen)
                s.playhead = std::fmod (s.playhead, loopLen);
            else if (s.playhead < 0.0)
                s.playhead = std::fmod (s.playhead, loopLen) + loopLen;
        }

        // ---- Onset detection ----
        if (onsetOn)
        {
            const float mono = nCh > 1 ? 0.5f * (in0 + in1) : in0;
            s.ring[(size_t) s.ringPos] = mono;
            if (++s.ringPos >= (int) s.ring.size())
                s.ringPos = 0;

            const bool armed = s.holdLeft <= 0.0;
            if (s.holdLeft > 0.0)
                s.holdLeft -= 1.0;

            // Onsets are ignored while a recording is already running (from the Capture button).
            if (s.detector.process (mono, armed && ! s.recording, onsetMap.ratio, onsetMap.floorFlux))
                onsetTrigger();
        }

        // ---- Record into the chosen layer ----
        if (s.recording)
        {
            float newL, newR;
            if (s.recStereo)
            {
                newL = in0 * s.inG;
                newR = in1 * s.senseG;
            }
            else
            {
                newL = newR = (nCh > 1 ? 0.5f * (in0 + in1) : in0) * s.inG;
            }
            recordFrame (s.recHead++, newL, newR);
            s.has[s.recLayer] = true;
            if (s.recHead >= s.recLimit)
            {
                s.recording = false;                // layer full, or an onset recording has run its time
                if (! s.recFromOnset)
                    s.captureRanOut.fetch_add (1);  // the plugin uses this to switch the Capture button off
            }
        }

        // ---- Strike click ----
        if (strikePending)
        {
            strikePending = false;
            if (grainsOn)
                startGrain (true);
            if (wtOn)
                startNote();
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

        // ---- The continuous oscillator picks a new place and pitch from time to time ----
        if (wtContOn)
        {
            s.wtCountdown -= 1.0;
            if (s.wtCountdown <= 0.0)
            {
                s.wtU = s.rng.uniform();
                const float deviation = map::pitchDeviationSemitones (params.pitchDeviation, s.rng.uniform(), scaleMask);
                s.wtDevRatio = std::exp2 (deviation / 12.0f);

                const auto pan = map::panSettings (modPan, s.rng.uniform(), s.rng.uniform());
                s.wtPanTargetL = std::cos (pan.angle);
                s.wtPanTargetR = std::sin (pan.angle);
                s.wtMonoBlend  = pan.monoBlend;

                const bool instant = s.wtInstant || ! s.osc.hasTable();
                s.osc.setFrequency (wtFrequency(), instant);
                wtRetarget (instant ? 0.0f : morphSamples);
                if (instant)
                {
                    s.wtPanL = s.wtPanTargetL;
                    s.wtPanR = s.wtPanTargetR;
                }
                s.wtInstant = false;

                double interval = (double) kWtEventSeconds * sr;
                if (! inten.sync)
                    interval *= 0.25 + 1.5 * (double) s.rng.uniform();
                s.wtCountdown += std::max (1.0, interval);
            }
        }

        // ---- Play all sounding grains ----
        float grainL = 0.0f, grainR = 0.0f;
        if (s.activeGrains > 0)
        {
            for (auto& g : s.grains)
            {
                if (! g.active)
                    continue;

                const float t = ((float) g.age + 0.5f) / (float) g.len;
                const float w = g.window.at (t);
                float l = readCubic (g.dataL, s.totalSamples, g.pos);
                float r = g.dataR != nullptr ? readCubic (g.dataR, s.totalSamples, g.pos) : l;
                if (g.monoBlend > 0.0f)
                {
                    const float m = 0.5f * (l + r);
                    l += g.monoBlend * (m - l);
                    r += g.monoBlend * (m - r);
                }
                grainL += l * w * g.gainL;
                grainR += r * w * g.gainR;

                g.pos += g.step;
                if (++g.age >= g.len)
                {
                    g.active = false;
                    --s.activeGrains;
                    if (g.strike) --s.activeStrike; else --s.activeContinuous;
                }
            }
        }

        // ---- Play the oscillator and any Strike notes ----
        float wtL = 0.0f, wtR = 0.0f;
        const float wtLevelTarget = (wtContOn && s.osc.hasTable()) ? 1.0f : 0.0f;
        s.wtLevel += (wtLevelTarget - s.wtLevel) * levelCoef;
        if (s.wtLevel > 1e-5f)
        {
            s.wtPanL += (s.wtPanTargetL - s.wtPanL) * panCoef;
            s.wtPanR += (s.wtPanTargetR - s.wtPanR) * panCoef;

            float l, r;
            s.osc.tick (l, r);
            if (s.wtMonoBlend > 0.0f)
            {
                const float m = 0.5f * (l + r);
                l += s.wtMonoBlend * (m - l);
                r += s.wtMonoBlend * (m - r);
            }
            wtL += l * s.wtPanL * s.wtLevel;
            wtR += r * s.wtPanR * s.wtLevel;
        }
        if (s.activeNotes > 0)
        {
            for (auto& n : s.notes)
            {
                if (! n.active)
                    continue;

                float l, r;
                n.voice.tick (l, r);
                const float e = n.window.at (((float) n.age + 0.5f) / (float) n.len);
                if (n.monoBlend > 0.0f)
                {
                    const float m = 0.5f * (l + r);
                    l += n.monoBlend * (m - l);
                    r += n.monoBlend * (m - r);
                }
                wtL += l * e * n.gainL;
                wtR += r * e * n.gainR;

                if (++n.age >= n.len)
                {
                    n.active = false;
                    --s.activeNotes;
                }
            }
        }

        // Bring the grain bus up to roughly the input's loudness (random left/right placement
        // shares each grain between two channels), then bend (not chop) any peaks.
        const float wetL = map::softSat ((grainL * s.gGrain + wtL * kWtGain * s.gWt) * 1.2f);
        const float wetR = map::softSat ((grainR * s.gGrain + wtR * kWtGain * s.gWt) * 1.2f);

        // ---- Mix ----
        float mixL, mixR;
        if (nCh == 1)
        {
            mixL = mixR = s.dryG * in0 + s.wetG * 0.70710678f * (wetL + wetR);
        }
        else
        {
            mixL = s.dryG * in0 + s.wetG * wetL;
            mixR = s.dryG * in1 + s.wetG * wetR;
        }

        // ---- Mod effects (only one target is active at a time) ----
        float outL = mixL, outR = mixR;
        if (revRun)
        {
            s.revDry += (revDryTarget - s.revDry) * fxCoef;
            s.revWet += (revWetTarget - s.revWet) * fxCoef;
            float rl, rr;
            s.reverb.process (mixL, mixR, rl, rr);
            outL = map::softSat (s.revDry * mixL + s.revWet * map::softSat (rl));
            outR = map::softSat (s.revDry * mixR + s.revWet * map::softSat (rr));
            s.revRunning = true;
        }
        if (dlyRun)
        {
            s.dlyLevel += (dlyTarget - s.dlyLevel) * fxCoef;
            float dl, dr;
            s.delay.process (mixL, mixR, dl, dr);
            outL += s.dlyLevel * map::softSat (dl);
            outR += s.dlyLevel * map::softSat (dr);
            s.dlyRunning = true;
        }

        // ---- Output ----
        float fadeG = s.outG;
        if (s.fadeLeft > 0)
        {
            const float t = 1.0f - (float) s.fadeLeft / (float) s.fadeTotal;
            fadeG *= t * t * (3.0f - 2.0f * t);
            --s.fadeLeft;
        }
        if (nCh == 1)
        {
            channels[0][i] = 0.5f * (outL + outR) * fadeG;
        }
        else
        {
            channels[0][i] = outL * fadeG;
            channels[1][i] = outR * fadeG;
        }
    }

    s.inG = targetIn;  s.senseG = targetSense;  s.dryG = targetDry;  s.wetG = targetWet;  s.outG = targetOut;
    if (s.recording)
        ++s.rev;                                    // the oscillator follows a recording as it grows
}

} // namespace ceo
