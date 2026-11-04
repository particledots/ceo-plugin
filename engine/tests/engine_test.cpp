// Self-test for the Ceo engine. Run automatically by the build; the build fails if any check fails.

#include "ceo/Engine.h"
#include "ceo/Mapping.h"
#include "ceo/Window.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <vector>

static int failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (! (cond)) {                                                    \
            std::printf ("FAIL line %d: %s   ", __LINE__, #cond);          \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static const double kSr = 48000.0;
static const int    kBlock = 256;

// Runs the engine over a stereo signal in blocks. 'in' is mono (copied to both channels).
// Returns the left and right outputs.
struct Run
{
    std::vector<float> L, R;
};

static Run process (ceo::Engine& e, const ceo::Params& p, const std::vector<float>& in)
{
    Run out;
    out.L = in;
    out.R = in;
    for (size_t pos = 0; pos < in.size(); pos += kBlock)
    {
        const int n = (int) std::min<size_t> (kBlock, in.size() - pos);
        float* ch[2] = { out.L.data() + pos, out.R.data() + pos };
        e.setParams (p);
        e.process (ch, 2, n);
    }
    return out;
}

static std::vector<float> sine (double seconds, double freq, float amp)
{
    std::vector<float> v ((size_t) (seconds * kSr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * (float) std::sin (2.0 * 3.14159265358979 * freq * (double) i / kSr);
    return v;
}

static std::vector<float> silence (double seconds) { return std::vector<float> ((size_t) (seconds * kSr), 0.0f); }

static float rms (const std::vector<float>& v, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to && i < v.size(); ++i)
        sum += (double) v[i] * (double) v[i];
    return (float) std::sqrt (sum / (double) std::max<size_t> (1, to - from));
}

static bool allFiniteAndBounded (const Run& r, float bound)
{
    for (size_t i = 0; i < r.L.size(); ++i)
        if (! std::isfinite (r.L[i]) || ! std::isfinite (r.R[i]) || std::fabs (r.L[i]) > bound || std::fabs (r.R[i]) > bound)
            return false;
    return true;
}

// Records 'in' into a layer (0..5, or 6 = Omega, where 'scan' picks the layer), then switches Capture off.
static void recordInto (ceo::Engine& e, int layer, const std::vector<float>& in, float dub = 0.0f, float scan = 0.0f)
{
    ceo::Params p;
    p.dryWet = 0.0f;                        // dry only while recording
    p.layer  = layer;
    p.dub    = dub;
    p.scan   = scan;
    process (e, p, silence (0.02));         // a first block with Capture off, then press it
    p.capture = true;
    process (e, p, in);
    p.capture = false;
    process (e, p, silence (0.05));
    e.reset();                              // grains started while recording must not leak into what is measured next
}

// Records into layer alpha, replacing whatever was there.
static void record (ceo::Engine& e, const std::vector<float>& in)
{
    recordInto (e, 0, in, 0.0f);
}

// Strength of one pure tone in part of a signal (Goertzel). A sine of amplitude A gives about A*A.
static double tonePower (const std::vector<float>& v, size_t from, size_t to, double freq)
{
    to = std::min (to, v.size());
    const double w     = 2.0 * 3.14159265358979 * freq / kSr;
    const double coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double s0 = (double) v[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double n = (double) (to - from);
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / ((n * 0.5) * (n * 0.5));
}

// Flips a button, which counts as one click, and lets a couple of blocks go by.
static void click (ceo::Engine& e, ceo::Params& p, bool& flag)
{
    flag = ! flag;
    process (e, p, silence (0.012));
}

static void testMapping()
{
    using namespace ceo::map;

    CHECK (std::fabs (grainLengthSeconds (0.0f) - 0.004f) < 1e-5f, "length at 0 = %f", grainLengthSeconds (0.0f));
    CHECK (std::fabs (grainLengthSeconds (0.5f) - 0.75f)  < 1e-4f, "length at centre = %f", grainLengthSeconds (0.5f));
    CHECK (std::fabs (grainLengthSeconds (1.0f) - 3.0f)   < 1e-3f, "length at 1 = %f", grainLengthSeconds (1.0f));
    for (float k = 0.0f; k < 0.999f; k += 0.01f)
        CHECK (grainLengthSeconds (k + 0.01f) > grainLengthSeconds (k), "length not rising at %f", k);

    CHECK (pitchRatio (0.5f) == 1.0f, "pitch centre must be exactly 1");
    CHECK (std::fabs (pitchRatio (1.0f) - 4.0f)  < 1e-4f, "pitch top = %f", pitchRatio (1.0f));
    CHECK (std::fabs (pitchRatio (0.0f) - 0.25f) < 1e-4f, "pitch bottom = %f", pitchRatio (0.0f));

    CHECK (inputGain (0.5f) == 1.0f && outputGain (0.5f) == 1.0f, "centre must be unity");
    CHECK (inputGain (0.0f) == 0.0f && outputGain (0.0f) == 0.0f, "bottom must be silent");
    CHECK (std::fabs (inputGain (1.0f) - dbToGain (18.0f)) < 1e-3f, "input top");
    CHECK (std::fabs (outputGain (1.0f) - dbToGain (12.0f)) < 1e-3f, "output top");

    for (float x = -0.7f; x <= 0.7f; x += 0.05f)
        CHECK (softSat (x) == x, "softSat not linear at %f", x);
    float last = softSat (0.7f);
    for (float x = 0.7f; x < 50.0f; x += 0.1f)
    {
        const float y = softSat (x);
        CHECK (y >= last && y <= 1.0f, "softSat at %f = %f", x, y);
        last = y;
    }
    CHECK (softSat (-3.0f) == -softSat (3.0f), "softSat must be symmetric");

    const float mixes[] = { 0.0f, 0.3f, 0.5f, 0.8f, 1.0f };
    for (float m : mixes)
        CHECK (std::fabs (dryGain (m) * dryGain (m) + wetGain (m) * wetGain (m) - 1.0f) < 1e-5f, "equal power at %f", m);
    CHECK (dryGain (0.0f) == 1.0f && std::fabs (wetGain (0.0f)) < 1e-6f, "dry/wet at 0");

    // Intensity: most grains at centre, fewer towards either end, off at both extremes.
    CHECK (intensity (0.5f).active && std::fabs (intensity (0.5f).grains - 44.0f) < 0.01f, "centre = 44 grains");
    CHECK (! intensity (0.0f).active && ! intensity (1.0f).active, "ends must switch the continuous engine off");
    CHECK (intensity (0.3f).sync && ! intensity (0.7f).sync, "below centre is even, above is random");
    CHECK (intensity (0.3f).grains < 44.0f && intensity (0.2f).grains < intensity (0.3f).grains, "fewer grains away from centre");
    CHECK (intensity (0.7f).grains < 44.0f && intensity (0.8f).grains < intensity (0.7f).grains, "fewer grains away from centre");
}

static void testWindow()
{
    ceo::GrainWindow w;

    w.set (0.5f, 0.5f);
    CHECK (w.at (0.5f) > 0.999f, "bell peak = %f", w.at (0.5f));
    CHECK (w.at (0.0f) < 1e-3f && w.at (1.0f) < 1e-3f, "bell must end at zero");
    CHECK (w.at (0.25f) < w.at (0.4f) && w.at (0.75f) < w.at (0.6f), "bell must rise to the middle");

    w.set (0.0f, 0.5f);                                    // fully square
    CHECK (w.at (0.5f) > 0.999f && w.at (0.2f) > 0.999f && w.at (0.8f) > 0.999f, "square must be flat");
    CHECK (w.at (0.0f) < 1e-3f && w.at (1.0f) < 1e-3f, "square must start and end at zero (no clicks)");

    w.set (1.0f, 0.5f);                                    // fully ramp
    CHECK (w.at (0.05f) > w.at (0.5f) && w.at (0.5f) > w.at (0.9f), "ramp must descend");
    CHECK (w.at (0.99f) < 0.05f, "ramp must end near zero");

    // Between centre and an end the shape is a blend; every value stays in range.
    for (float k = 0.0f; k <= 1.0f; k += 0.05f)
    {
        w.set (k, 0.2f);
        for (float t = 0.0f; t <= 1.0f; t += 0.01f)
            CHECK (w.at (t) >= -1e-4f && w.at (t) <= 1.0001f, "window out of range k=%f t=%f v=%f", k, t, w.at (t));
        CHECK (w.loudnessCompensation() > 0.5f && w.loudnessCompensation() < 6.0f, "comp %f", w.loudnessCompensation());
    }
}

static void testDryPassThrough()
{
    // Dry/wet fully dry, Output level at centre: audio comes out exactly as it went in.
    for (int channels : { 1, 2 })
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, channels);
        ceo::Params p;
        p.dryWet = 0.0f;
        auto in = sine (0.5, 330.0, 0.8f);
        std::vector<float> a = in, b = in;
        for (size_t pos = 0; pos < in.size(); pos += kBlock)
        {
            const int n = (int) std::min<size_t> (kBlock, in.size() - pos);
            float* ch[2] = { a.data() + pos, b.data() + pos };
            e.setParams (p);
            e.process (ch, channels, n);
        }
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i)
            if (a[i] != in[i] || (channels == 2 && b[i] != in[i])) { same = false; break; }
        CHECK (same, "dry-only output must equal input (channels=%d)", channels);
    }
}

static void testSilentWhenEmpty()
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    ceo::Params p;
    p.dryWet = 1.0f;
    auto out = process (e, p, silence (1.0));
    CHECK (rms (out.L, 0, out.L.size()) == 0.0f, "nothing recorded yet, so no sound");
    CHECK (e.activeGrainCount() == 0, "no grains without a recording");
    CHECK (! e.layerHasContent(), "empty layer");
}

static void testRecording()
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);

    ceo::Params p;
    p.dryWet = 0.0f;

    // Capture switch already on at start does nothing.
    p.capture = true;
    process (e, p, silence (0.1));
    CHECK (! e.isRecording(), "capture that is already on at load must not record");
    p.capture = false;
    process (e, p, silence (0.1));

    // Press: records. Press again: stops.
    p.capture = true;
    process (e, p, sine (0.2, 440.0, 0.5f));
    CHECK (e.isRecording(), "should be recording");
    CHECK (e.layerHasContent(), "layer should have content");
    p.capture = false;
    process (e, p, silence (0.05));
    CHECK (! e.isRecording(), "should have stopped");

    // Stops by itself when the layer (10 s + 3 s tail) is full, even with the switch still on.
    p.capture = true;
    process (e, p, silence (12.5));
    CHECK (e.isRecording(), "still recording at 12.5 s");
    process (e, p, silence (0.7));
    CHECK (! e.isRecording(), "must stop by itself after 13 s");
    CHECK (e.captureRanOutCount() == 1, "running out of room is reported once (%u)", (unsigned) e.captureRanOutCount());
    process (e, p, silence (0.5));
    CHECK (e.captureRanOutCount() == 1, "and not reported again while the switch stays on (%u)", (unsigned) e.captureRanOutCount());
    p.capture = false;
    process (e, p, silence (0.05));
    CHECK (e.captureRanOutCount() == 1, "switching Capture off does not count as running out (%u)", (unsigned) e.captureRanOutCount());
    p.capture = true;
    process (e, p, sine (0.2, 440.0, 0.5f));
    p.capture = false;
    process (e, p, silence (0.05));
    CHECK (e.captureRanOutCount() == 1, "a short recording stopped by hand is not reported (%u)", (unsigned) e.captureRanOutCount());

    // Recording goes into whichever layer the dial points at.
    ceo::Engine e2;
    e2.prepare (kSr, kBlock, 2);
    recordInto (e2, 2, sine (0.1, 440.0, 0.5f));
    CHECK (e2.layerHasContent (2) && ! e2.layerHasContent (0) && ! e2.layerHasContent (1) && ! e2.layerHasContent (5),
           "recording must land in layer gamma only");

    // Turning the layer dial while recording does not move the recording.
    ceo::Engine e3;
    e3.prepare (kSr, kBlock, 2);
    ceo::Params q;
    q.dryWet = 0.0f;
    q.layer = 1;
    process (e3, q, silence (0.02));
    q.capture = true;
    process (e3, q, sine (0.1, 440.0, 0.5f));
    CHECK (e3.recordingLayer() == 1, "recording layer beta");
    q.layer = 3;
    process (e3, q, sine (0.1, 440.0, 0.5f));
    CHECK (e3.recordingLayer() == 1, "dial change must not move a recording in progress");
    q.capture = false;
    process (e3, q, silence (0.05));
    CHECK (e3.layerHasContent (1) && ! e3.layerHasContent (3), "recording stayed in beta");
}

static void testGrainsMakeSound()
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    record (e, sine (3.0, 220.0, 0.5f));

    ceo::Params p;
    p.dryWet = 1.0f;          // all granular
    p.length = 0.3f;          // short-ish grains
    p.scan   = 0.1f;          // inside the 3 s we recorded (0.1 x 10 s = 1 s)
    auto out = process (e, p, silence (3.0));

    CHECK (allFiniteAndBounded (out, 2.0f), "output must be finite and not huge");
    const float level = rms (out.L, (size_t) (1.0 * kSr), out.L.size());
    CHECK (level > 0.02f, "grains should be audible, rms = %f", level);
    CHECK (e.activeGrainCount() > 0, "grains should be playing");
    CHECK (rms (out.L, (size_t) (1.0 * kSr), out.L.size()) > 0.0f && rms (out.R, (size_t) (1.0 * kSr), out.R.size()) > 0.0f,
           "both channels should carry sound");

    // Output level at the bottom silences everything.
    p.outputLevel = 0.0f;
    auto quiet = process (e, p, silence (0.5));
    CHECK (rms (quiet.L, (size_t) (0.1 * kSr), quiet.L.size()) < 1e-6f, "output level 0 = silence");
}

static void testLoudnessIsRoughlyConsistent()
{
    // Whatever the Intensity, the granular output should stay in the same loudness neighbourhood as the input.
    std::vector<float> noise (3 * (size_t) kSr);
    uint32_t s = 12345u;
    for (auto& x : noise) { s = s * 1664525u + 1013904223u; x = 0.3f * ((float) (s >> 8) / 8388608.0f - 1.0f); }
    const float inputRms = rms (noise, 0, noise.size());

    const float intensities[] = { 0.5f, 0.35f, 0.2f, 0.65f, 0.8f };
    const float lengths[]     = { 0.2f, 0.5f, 0.8f };
    const float windows[]     = { 0.0f, 0.5f, 1.0f };
    for (float in : intensities)
        for (float len : lengths)
            for (float win : windows)
            {
                ceo::Engine e;
                e.prepare (kSr, kBlock, 2);
                record (e, noise);
                ceo::Params p;
                p.dryWet = 1.0f; p.intensity = in; p.length = len; p.grainWindow = win; p.scan = 0.05f; p.spray = 0.1f;
                auto out = process (e, p, silence (6.0));
                const float level = rms (out.L, (size_t) (3.0 * kSr), out.L.size());
                const float ratio = level / inputRms;
                if (ratio < 0.2f || ratio > 4.0f)
                    std::printf ("  loudness ratio %.2f at intensity %.2f length %.2f window %.2f\n", ratio, in, len, win);
                CHECK (ratio > 0.2f && ratio < 4.0f, "output %f vs input %f", level, inputRms);
            }
}

static void testIntensity()
{
    std::vector<float> src = sine (3.0, 220.0, 0.5f);

    // Centre: lots of grains at once. A quarter-turn: fewer. Both ends: none.
    int centre = 0, quarter = 0;
    {
        ceo::Engine e; e.prepare (kSr, kBlock, 2); record (e, src);
        ceo::Params p; p.dryWet = 1.0f; p.length = 0.5f; p.intensity = 0.5f;
        process (e, p, silence (2.0));
        centre = e.activeGrainCount();
    }
    {
        ceo::Engine e; e.prepare (kSr, kBlock, 2); record (e, src);
        ceo::Params p; p.dryWet = 1.0f; p.length = 0.5f; p.intensity = 0.25f;
        process (e, p, silence (2.0));
        quarter = e.activeGrainCount();
    }
    CHECK (centre >= 40 && centre <= 44, "centre grain count %d", centre);
    CHECK (quarter >= 3 && quarter <= 12, "quarter-turn grain count %d", quarter);

    for (float k : { 0.0f, 1.0f })
    {
        ceo::Engine e; e.prepare (kSr, kBlock, 2); record (e, src);
        ceo::Params p; p.dryWet = 1.0f; p.length = 0.3f; p.intensity = k;
        auto out = process (e, p, silence (4.0));
        CHECK (e.activeGrainCount() == 0, "extreme intensity %f: grains left %d", k, e.activeGrainCount());
        CHECK (rms (out.L, (size_t) (3.0 * kSr), out.L.size()) == 0.0f, "extreme intensity %f must be silent", k);
    }
}

static float slopeInMiddleOfFirstGrain (float direction)
{
    // Record a slow rising ramp, then play short grains with a square window and nearly one grain at a time.
    std::vector<float> ramp ((size_t) (2.0 * kSr));
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = 0.5f * (float) i / (float) ramp.size();

    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    record (e, ramp);

    ceo::Params p;
    p.dryWet = 1.0f;
    p.length = 0.35f;                 // about 50 ms
    p.intensity = 0.02f;              // almost one grain at a time, evenly spaced
    p.grainWindow = 0.0f;             // square
    p.grainDirection = direction;
    p.scan = 0.05f;                   // 0.5 s into the layer
    auto out = process (e, p, silence (0.2));

    const double len = ceo::map::grainLengthSeconds (0.35f) * kSr;
    const size_t a = (size_t) (len * 0.3), b = (size_t) (len * 0.7);
    return (out.L[b] + out.R[b]) - (out.L[a] + out.R[a]);
}

static void testDirection()
{
    const float forward  = slopeInMiddleOfFirstGrain (0.0f);
    const float backward = slopeInMiddleOfFirstGrain (1.0f);
    CHECK (forward  > 0.0f, "direction 0 must play forwards, slope %g", forward);
    CHECK (backward < 0.0f, "direction 1 must play backwards, slope %g", backward);
}

static void testSprayAndScanStayInsideLayer()
{
    // Extreme settings must never crash or produce junk, including scan at the very end with long, high-pitched grains.
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    record (e, sine (2.0, 330.0, 0.5f));

    ceo::Params p;
    p.dryWet = 1.0f; p.scan = 1.0f; p.spray = 1.0f; p.length = 1.0f; p.pitch = 1.0f; p.grainDirection = 0.5f;
    auto out = process (e, p, silence (4.0));
    CHECK (allFiniteAndBounded (out, 2.0f), "extreme scan/spray/pitch must stay finite");

    p.pitch = 0.0f; p.scan = 0.0f; p.spray = 0.0f; p.length = 0.0f;
    out = process (e, p, silence (2.0));
    CHECK (allFiniteAndBounded (out, 2.0f), "shortest grains at lowest pitch must stay finite");
}

static void testMono()
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 1);

    auto in = sine (1.0, 440.0, 0.5f);
    ceo::Params p;
    p.dryWet = 0.0f;
    {
        std::vector<float> first = silence (0.02);          // first block with Capture off
        float* ch[1] = { first.data() };
        e.setParams (p);
        e.process (ch, 1, (int) first.size());
    }
    p.capture = true;
    std::vector<float> buf = in;
    for (size_t pos = 0; pos < buf.size(); pos += kBlock)
    {
        float* ch[1] = { buf.data() + pos };
        e.setParams (p);
        e.process (ch, 1, (int) std::min<size_t> (kBlock, buf.size() - pos));
    }
    p.capture = false; p.dryWet = 1.0f; p.scan = 0.02f;
    std::vector<float> quiet = silence (2.0);
    for (size_t pos = 0; pos < quiet.size(); pos += kBlock)
    {
        float* ch[1] = { quiet.data() + pos };
        e.setParams (p);
        e.process (ch, 1, (int) std::min<size_t> (kBlock, quiet.size() - pos));
    }
    CHECK (rms (quiet, (size_t) (1.0 * kSr), quiet.size()) > 0.02f, "mono in / mono out should make sound");
}

static void testFuzz()
{
    // Random control settings, including odd block sizes: always finite, never enormous.
    uint32_t s = 987654321u;
    auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.0f; };

    for (int round = 0; round < 60; ++round)
    {
        ceo::Engine e;
        const int channels = 1 + (round & 1);
        e.prepare (kSr, 512, channels);

        for (int step = 0; step < 6; ++step)
        {
            ceo::Params p;
            p.inputLevel = rnd(); p.dryWet = rnd(); p.outputLevel = rnd();
            p.capture = rnd() > 0.5f; p.strike = rnd() > 0.5f; p.layer = (int) (rnd() * 6.99f);
            p.erase = rnd() > 0.5f; p.undo = rnd() > 0.5f; p.follow = rnd() > 0.5f;
            p.intensity = rnd(); p.grainWindow = rnd(); p.grainDirection = rnd(); p.length = rnd();
            p.scan = rnd(); p.spray = rnd(); p.pitch = rnd(); p.pitchDeviation = rnd();
            p.modAmount = rnd(); p.hold = rnd(); p.sense = rnd(); p.dub = rnd();
            p.stereoInput = rnd() > 0.5f; p.onsetCapture = rnd() > 0.5f; p.modTarget = (int) (rnd() * 3.99f);

            const int n = 1 + (int) (rnd() * 700.0f);
            std::vector<float> a ((size_t) n), b ((size_t) n);
            for (int i = 0; i < n; ++i) { a[(size_t) i] = 1.5f * (rnd() * 2.0f - 1.0f); b[(size_t) i] = a[(size_t) i]; }
            float* ch[2] = { a.data(), b.data() };
            e.setParams (p);
            e.process (ch, channels, n);
            for (int i = 0; i < n; ++i)
                CHECK (std::isfinite (a[(size_t) i]) && std::fabs (a[(size_t) i]) < 40.0f, "fuzz round %d step %d sample %d = %f", round, step, i, a[(size_t) i]);
        }
    }

    // Non-finite input must not poison the output.
    ceo::Engine e;
    e.prepare (kSr, 64, 2);
    ceo::Params p; p.capture = true; p.dryWet = 0.5f;
    std::vector<float> a (64, NAN), b (64, INFINITY);
    float* ch[2] = { a.data(), b.data() };
    e.setParams (p);
    e.process (ch, 2, 64);
    for (int i = 0; i < 64; ++i)
        CHECK (std::isfinite (a[(size_t) i]) && std::isfinite (b[(size_t) i]), "NaN/inf input leaked to output");
}

static void testResetKeepsRecording()
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    record (e, sine (1.0, 220.0, 0.5f));
    CHECK (e.layerHasContent(), "recorded");
    e.prepare (kSr, 128, 2);          // same sample rate again (Logic does this)
    CHECK (e.layerHasContent(), "recording must survive prepare() at the same sample rate");
    e.reset();
    CHECK (e.layerHasContent(), "recording must survive reset()");
    CHECK (e.activeGrainCount() == 0, "reset silences grains");
}

// ---------------------------------------------------------------------------
// Milestone 4
// ---------------------------------------------------------------------------

// Plays one layer (or Omega) at a Scan position and returns the left channel.
static std::vector<float> playLayer (ceo::Engine& e, int layer, float scan, double seconds = 2.5)
{
    ceo::Params p;
    p.dryWet = 1.0f; p.layer = layer; p.scan = scan; p.length = 0.45f;     // grains of about 0.4 s
    return process (e, p, silence (seconds)).L;
}

static void testLayersAndOmega()
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    recordInto (e, 0, sine (3.0, 220.0, 0.3f));
    recordInto (e, 2, sine (3.0, 880.0, 0.3f));
    CHECK (e.layerHasContent (0) && e.layerHasContent (2) && ! e.layerHasContent (1) && ! e.layerHasContent (5),
           "alpha and gamma recorded, nothing else");

    const size_t from = (size_t) (1.0 * kSr);

    auto a = playLayer (e, 0, 0.1f);
    CHECK (tonePower (a, from, a.size(), 220.0) > 20.0 * tonePower (a, from, a.size(), 880.0) + 1e-6, "layer alpha plays its own tone");
    auto g = playLayer (e, 2, 0.1f);
    CHECK (tonePower (g, from, g.size(), 880.0) > 20.0 * tonePower (g, from, g.size(), 220.0) + 1e-6, "layer gamma plays its own tone");
    auto b = playLayer (e, 1, 0.1f);
    CHECK (rms (b, (size_t) (0.6 * kSr), b.size()) == 0.0f, "an empty layer is silent");

    // Omega: the six layers lie end to end along Scan (each one sixth of the knob).
    auto o0 = playLayer (e, 6, 0.02f);
    CHECK (tonePower (o0, from, o0.size(), 220.0) > 20.0 * tonePower (o0, from, o0.size(), 880.0) + 1e-6, "Omega, Scan in alpha's sixth");
    auto o2 = playLayer (e, 6, 2.0f / 6.0f + 0.02f);
    CHECK (tonePower (o2, from, o2.size(), 880.0) > 20.0 * tonePower (o2, from, o2.size(), 220.0) + 1e-6, "Omega, Scan in gamma's sixth");
    auto o1 = playLayer (e, 6, 1.0f / 6.0f + 0.02f);
    CHECK (rms (o1, (size_t) (0.6 * kSr), o1.size()) == 0.0f, "Omega, Scan over an empty layer is silent");

    // In Omega, Capture records into the layer under the Scan knob.
    ceo::Engine e2;
    e2.prepare (kSr, kBlock, 2);
    recordInto (e2, 6, sine (0.2, 440.0, 0.5f), 0.0f, 4.0f / 6.0f + 0.02f);
    CHECK (e2.layerHasContent (4) && ! e2.layerHasContent (0) && ! e2.layerHasContent (3) && ! e2.layerHasContent (5),
           "Omega capture lands in epsilon");

    // Spray in Omega can reach every layer: with Spray at maximum and content in two layers, both sound.
    ceo::Engine e3;
    e3.prepare (kSr, kBlock, 2);
    recordInto (e3, 0, sine (3.0, 220.0, 0.3f));
    recordInto (e3, 5, sine (3.0, 880.0, 0.3f));
    ceo::Params p;
    p.dryWet = 1.0f; p.layer = 6; p.spray = 1.0f; p.length = 0.3f;
    auto sp = process (e3, p, silence (30.0)).L;
    double best220 = 0.0, best880 = 0.0;
    const size_t chunk = (size_t) (0.25 * kSr);
    for (size_t pos = 0; pos + chunk <= sp.size(); pos += chunk)
    {
        best220 = std::max (best220, tonePower (sp, pos, pos + chunk, 220.0));
        best880 = std::max (best880, tonePower (sp, pos, pos + chunk, 880.0));
    }
    CHECK (best220 > 1e-3 && best880 > 1e-3, "Omega with full Spray reaches both layers (%g, %g)", best220, best880);
}

static void testDubUndoErase()
{
    const auto tone = sine (3.0, 330.0, 0.3f);
    const auto quiet = silence (3.0);

    auto level = [] (ceo::Engine& e)
    {
        auto out = playLayer (e, 0, 0.1f, 3.0);
        return rms (out, (size_t) (1.5 * kSr), out.size());
    };

    ceo::Engine base;
    base.prepare (kSr, kBlock, 2);
    recordInto (base, 0, tone);
    const float baseLevel = level (base);
    CHECK (baseLevel > 0.02f, "baseline level %f", baseLevel);

    // Recording silence over a layer: Dub down replaces it, centre keeps half, full keeps all.
    struct Case { float dub; float lo; float hi; };
    const Case cases[] = { { 0.0f, 0.0f, 1e-6f }, { 0.5f, 0.40f, 0.60f }, { 1.0f, 0.90f, 1.10f } };
    for (const Case& c : cases)
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        recordInto (e, 0, tone);
        recordInto (e, 0, quiet, c.dub);
        const float ratio = level (e) / baseLevel;
        CHECK (ratio >= c.lo && ratio <= c.hi, "dub %.2f: level ratio %f (wanted %f to %f)", c.dub, ratio, c.lo, c.hi);
    }

    // Full Dub is sound on sound: both tones are there.
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        recordInto (e, 0, tone);
        recordInto (e, 0, sine (3.0, 880.0, 0.3f), 1.0f);
        auto out = playLayer (e, 0, 0.1f, 3.0);
        const size_t from = (size_t) (1.0 * kSr);
        const double other = tonePower (out, from, out.size(), 1500.0) + 1e-9;
        CHECK (tonePower (out, from, out.size(), 330.0) > 50.0 * other, "old tone still there");
        CHECK (tonePower (out, from, out.size(), 880.0) > 50.0 * other, "new tone added");
    }

    // Undo (one step) and Erase.
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        recordInto (e, 0, sine (3.0, 220.0, 0.3f));
        CHECK (e.undoLayer() == -1, "nothing to undo after recording into an empty layer");
        recordInto (e, 0, sine (3.0, 880.0, 0.3f), 0.0f);
        CHECK (e.undoLayer() == 0, "undo available after recording over a layer");

        ceo::Params p;
        p.dryWet = 1.0f; p.length = 0.45f; p.scan = 0.1f;
        const size_t from = (size_t) (1.0 * kSr);
        auto hear = [&] (double f1, double f2)
        {
            auto out = process (e, p, silence (2.5)).L;
            return tonePower (out, from, out.size(), f1) > 20.0 * tonePower (out, from, out.size(), f2) + 1e-6;
        };

        CHECK (hear (880.0, 220.0), "after recording over: the new sound");
        click (e, p, p.undo);
        CHECK (e.undoLayer() == -1, "undo is used up");
        CHECK (hear (220.0, 880.0), "after Undo: the first recording is back");
        click (e, p, p.undo);
        CHECK (hear (220.0, 880.0), "a second Undo changes nothing");
        click (e, p, p.erase);
        CHECK (! e.layerHasContent (0), "Erase empties the layer");
        auto out = process (e, p, silence (1.0)).L;
        CHECK (rms (out, (size_t) (0.5 * kSr), out.size()) == 0.0f, "silent after Erase");
        CHECK (e.undoLayer() == 0, "Erase of a layer with sound can be undone");
        click (e, p, p.undo);
        CHECK (e.layerHasContent (0), "Undo brings the erased layer back");
        CHECK (hear (220.0, 880.0), "after Erase and Undo: the sound is back");
        click (e, p, p.erase);
        click (e, p, p.erase);
        CHECK (! e.layerHasContent (0) && e.undoLayer() == 0, "Erasing an empty layer keeps the earlier copy");
    }

    // Erase and Undo act on the selected layer only.
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        recordInto (e, 0, tone);
        recordInto (e, 1, tone);
        ceo::Params p;
        p.layer = 1;
        process (e, p, silence (0.012));          // first block after a reset only notes where the buttons are
        click (e, p, p.erase);
        CHECK (e.layerHasContent (0) && ! e.layerHasContent (1), "Erase on beta leaves alpha alone");
    }
}

static float grainPower (bool strike)
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    record (e, sine (2.0, 220.0, 0.2f));

    ceo::Params p;
    p.dryWet = 1.0f; p.length = 0.35f; p.grainWindow = 0.0f; p.scan = 0.05f;
    p.intensity = strike ? 0.0f : 0.02f;          // Strike: continuous engine off. Otherwise about one grain at a time.

    if (strike)
    {
        process (e, p, silence (0.005));          // one block first, so the click below is a change
        p.strike = true;
    }
    auto out = process (e, p, silence (0.3));

    const double len = ceo::map::grainLengthSeconds (0.35f) * kSr;
    const size_t a = (size_t) (len * 0.3), b = (size_t) (len * 0.7);
    const float l = rms (out.L, a, b), r = rms (out.R, a, b);
    return std::sqrt (l * l + r * r);
}

static void testStrike()
{
    CHECK (ceo::map::kStrikeGain == 1.15f, "Strike grains are 15%% louder");

    const float cont = grainPower (false);
    const float hit  = grainPower (true);
    CHECK (cont > 0.01f && hit > 0.01f, "both make sound: %f %f", cont, hit);
    CHECK (hit / cont > 1.05f && hit / cont < 1.30f, "Strike grain should be about 15%% louder: ratio %f", hit / cont);

    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    record (e, sine (3.0, 220.0, 0.2f));

    ceo::Params p;
    p.dryWet = 1.0f; p.intensity = 0.0f; p.length = 0.4f; p.scan = 0.05f;     // continuous engine off, grains of about 0.2 s
    auto out = process (e, p, silence (0.5));
    CHECK (rms (out.L, 0, out.L.size()) == 0.0f && e.activeGrainCount() == 0, "with Intensity at the end, nothing plays by itself");

    click (e, p, p.strike);
    CHECK (e.activeStrikeGrainCount() == 1 && e.activeGrainCount() == 1, "one click, one grain");
    for (int i = 0; i < 3; ++i)
        click (e, p, p.strike);
    CHECK (e.activeStrikeGrainCount() == 4, "four clicks, four grains (got %d)", e.activeStrikeGrainCount());
    out = process (e, p, silence (0.1));
    CHECK (rms (out.L, 0, out.L.size()) > 0.01f, "Strike grains are audible");

    // Many clicks at once never exceed the Strike engine's share of 44.
    ceo::Engine e2;
    e2.prepare (kSr, kBlock, 2);
    record (e2, sine (3.0, 220.0, 0.2f));
    ceo::Params q;
    q.dryWet = 1.0f; q.intensity = 1.0f; q.length = 1.0f; q.scan = 0.05f;      // 3 s grains
    process (e2, q, silence (0.05));
    for (int i = 0; i < 70; ++i)
        click (e2, q, q.strike);
    CHECK (e2.activeStrikeGrainCount() == ceo::kMaxStrikeGrains, "Strike engine limited to %d grains (got %d)", ceo::kMaxStrikeGrains, e2.activeStrikeGrainCount());
    auto loud = process (e2, q, silence (1.0));
    CHECK (allFiniteAndBounded (loud, 2.0f), "70 overlapping Strike grains stay in range");

    // Strike and the continuous engine share the total of 88.
    ceo::Engine e3;
    e3.prepare (kSr, kBlock, 2);
    record (e3, sine (3.0, 220.0, 0.2f));
    ceo::Params r;
    r.dryWet = 1.0f; r.intensity = 0.5f; r.length = 1.0f; r.scan = 0.05f;
    process (e3, r, silence (1.0));
    for (int i = 0; i < 70; ++i)
        click (e3, r, r.strike);
    CHECK (e3.activeGrainCount() <= ceo::kMaxGrains, "never more than %d grains (got %d)", ceo::kMaxGrains, e3.activeGrainCount());
    CHECK (e3.activeStrikeGrainCount() == ceo::kMaxStrikeGrains, "Strike share intact while the continuous engine runs");
}

static void testPitchDeviation()
{
    using namespace ceo::map;

    // Mapping
    for (float u = 0.0f; u < 1.0f; u += 0.01f)
        CHECK (pitchDeviationSemitones (0.5f, u) == 0.0f, "centre must give no deviation");

    float lo = 0.0f, hi = 0.0f;
    int fractional = 0;
    for (float u = 0.0f; u < 1.0f; u += 0.001f)
    {
        const float v = pitchDeviationSemitones (0.0f, u);
        lo = std::min (lo, v);
        hi = std::max (hi, v);
        if (std::fabs (v - std::round (v)) > 0.05f)
            ++fractional;
    }
    CHECK (lo < -11.9f && hi > 11.9f && lo >= -12.0f && hi <= 12.0f, "free side spans +-12 at the bottom: %f %f", lo, hi);
    CHECK (fractional > 700, "free side is not quantised (%d of 1000 off the semitone grid)", fractional);

    float quarterMax = 0.0f;
    for (float u = 0.0f; u < 1.0f; u += 0.001f)
        quarterMax = std::max (quarterMax, std::fabs (pitchDeviationSemitones (0.25f, u)));
    CHECK (quarterMax > 5.0f && quarterMax <= 6.1f, "free side at quarter turn reaches about 6: %f", quarterMax);

    auto quantisedReach = [] (float k, bool& allWhole, int& distinct)
    {
        bool seen[25] = {};
        int  reach = 0;
        allWhole = true;
        for (float u = 0.0f; u < 1.0f; u += 0.0005f)
        {
            const float v = pitchDeviationSemitones (k, u);
            if (v != std::round (v))
                allWhole = false;
            reach = std::max (reach, (int) std::fabs (v));
            const int idx = (int) std::lround (v) + 12;
            if (idx >= 0 && idx < 25)
                seen[idx] = true;
        }
        distinct = 0;
        for (bool b : seen)
            distinct += b ? 1 : 0;
        return reach;
    };

    bool whole = false;
    int  distinct = 0;
    CHECK (quantisedReach (0.55f, whole, distinct) == 1 && whole && distinct == 3, "just above centre: -1, 0, +1 only");
    int mid = quantisedReach (0.75f, whole, distinct);
    CHECK (mid >= 5 && mid <= 7 && whole, "three-quarters: whole semitones, reach %d", mid);
    CHECK (quantisedReach (1.0f, whole, distinct) == 12 && whole && distinct == 25, "top: every step from -12 to +12 (%d)", distinct);

    // In the engine: with deviation, the original pitch is no longer the only pitch.
    auto tone440 = [] (float deviation)
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, sine (3.0, 440.0, 0.3f));
        ceo::Params p;
        p.dryWet = 1.0f; p.length = 0.45f; p.scan = 0.1f; p.pitchDeviation = deviation;
        auto out = process (e, p, silence (3.0)).L;
        return tonePower (out, (size_t) (1.0 * kSr), out.size(), 440.0);
    };
    const double none  = tone440 (0.5f);
    const double free  = tone440 (0.0f);
    const double steps = tone440 (1.0f);
    CHECK (none > 1e-4, "baseline tone is there");
    CHECK (free  < 0.3 * none, "free deviation spreads the pitch (%g vs %g)", free, none);
    CHECK (steps < 0.3 * none, "semitone deviation spreads the pitch (%g vs %g)", steps, none);
}

// Knob position that gives a wanted Follow speed (searches the mapping, so the test does not depend on its constants).
static float knobForSpeed (float wanted)
{
    float best = 0.5f, bestError = 1e9f;
    for (float k = 0.5f; k <= 1.0001f; k += 0.0005f)
    {
        const float err = std::fabs (ceo::map::followSpeed (k) - wanted);
        if (err < bestError) { bestError = err; best = k; }
    }
    return best;
}

static void testFollow()
{
    using namespace ceo::map;

    CHECK (followSpeed (0.5f) == 0.0f, "centre = stalled");
    CHECK (std::fabs (followSpeed (1.0f) - 20.0f) < 1e-3f && std::fabs (followSpeed (0.0f) + 20.0f) < 1e-3f, "ends = 20x forwards and backwards");
    CHECK (followSpeed (0.6f) > 0.0f && followSpeed (0.4f) < 0.0f, "above centre forwards, below backwards");
    for (float k = 0.55f; k < 0.999f; k += 0.01f)
        CHECK (followSpeed (k + 0.01f) > followSpeed (k), "speed must rise with the knob (%f)", k);
    const float slowest = followSpeed (0.5f + 0.04f / 2.0f + 0.0005f);
    CHECK (std::fabs (slowest - 0.05f) < 0.005f, "slowest speed is about 1/20 (%f)", slowest);

    // A rising ramp (0 to 0.2 over 2 s) in layer alpha, then silence. With square grains the output level
    // follows the ramp value at the playhead. (A slow ramp is nearly constant, so overlapping grains add up
    // rather than blend: a few grains and a low level keep the sum well clear of saturation.)
    std::vector<float> ramp ((size_t) (2.0 * kSr));
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = 0.2f * (float) i / (float) ramp.size();

    auto run = [&] (float scanKnob, double seconds)
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, ramp);
        ceo::Params p;
        p.dryWet = 1.0f; p.hold = 1.0f; p.follow = true; p.scan = scanKnob; p.length = 0.3f; p.grainWindow = 0.0f; p.intensity = 0.2f;
        return process (e, p, silence (seconds)).L;
    };
    auto level = [] (const std::vector<float>& v, double t0, double t1) { return rms (v, (size_t) (t0 * kSr), (size_t) (t1 * kSr)); };

    // Normal speed
    auto one = run (knobForSpeed (1.0f), 3.0);
    const float a = level (one, 0.4, 0.6), b = level (one, 1.4, 1.6);
    CHECK (a > 0.005f, "follow plays: %f", a);
    CHECK (b / a > 2.3f && b / a < 3.7f, "at speed 1 the level follows the ramp (1.5 s vs 0.5 s): ratio %f", b / a);
    CHECK (level (one, 2.4, 3.0) < 0.1f * b, "silent once the playhead is past the recording");

    // Stalled: the playhead stays at the very start of the ramp (nearly zero).
    auto stalled = run (0.5f, 2.0);
    CHECK (level (stalled, 1.0, 2.0) < 0.1f * b, "stalled playhead keeps playing the start of the layer");

    // Double speed gets there twice as fast: the level at 0.5 s matches normal speed at about 1 s.
    auto two = run (knobForSpeed (2.0f), 2.5);
    const float c = level (two, 0.4, 0.6), d = level (one, 0.9, 1.1);
    CHECK (c / d > 0.75f && c / d < 1.33f, "double speed: level at 0.5 s %f vs normal speed at 1.0 s %f", c, d);
    CHECK (level (two, 1.3, 2.0) < 0.15f * b, "double speed is past the recording after 1 s");

    // Backwards: the playhead starts at the end of the layer and runs back, so the ramp is met on the way, high to low.
    auto back = run (1.0f - knobForSpeed (1.0f), 10.0);
    const float e1 = level (back, 8.4, 8.6), e2 = level (back, 9.4, 9.6);
    CHECK (level (back, 4.0, 6.0) < 0.1f * b, "backwards: silent in the empty part");
    CHECK (e1 / e2 > 2.3f && e1 / e2 < 3.7f, "backwards: level falls as the playhead nears the start: %f vs %f", e1, e2);

    // Strike restarts the playhead. Capture restarts it too.
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, ramp);
        ceo::Params p;
        p.dryWet = 1.0f; p.hold = 1.0f; p.follow = true; p.scan = knobForSpeed (1.0f); p.length = 0.3f; p.grainWindow = 0.0f; p.intensity = 0.2f;
        process (e, p, silence (1.5));
        CHECK (std::fabs (e.playheadSeconds() - 1.5) < 0.05, "playhead after 1.5 s at normal speed: %f", e.playheadSeconds());
        click (e, p, p.strike);
        auto after = process (e, p, silence (0.5)).L;
        CHECK (std::fabs (e.playheadSeconds() - 0.51) < 0.06, "Strike sends the playhead back to the start: %f", e.playheadSeconds());
        CHECK (level (after, 0.4, 0.5) < 0.55f * b, "level drops back after Strike");
        CHECK (e.activeStrikeGrainCount() == 0, "in Follow mode Strike restarts playback instead of firing a grain");

        process (e, p, silence (1.0));
        p.capture = true;
        process (e, p, silence (0.2));
        CHECK (e.playheadSeconds() < 0.25, "Capture restarts the playhead: %f", e.playheadSeconds());
    }

    // Switching Follow off and on again starts from the beginning.
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, ramp);
        ceo::Params p;
        p.dryWet = 1.0f; p.hold = 1.0f; p.follow = true; p.scan = knobForSpeed (1.0f);
        process (e, p, silence (1.0));
        p.follow = false;
        process (e, p, silence (0.5));
        p.follow = true;
        process (e, p, silence (0.2));
        CHECK (e.playheadSeconds() < 0.3, "Follow switched on again: playhead near the start (%f)", e.playheadSeconds());
    }

    // Follow works in Omega too: the playhead runs on through the layers.
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, ramp);
        ceo::Params p;
        p.dryWet = 1.0f; p.hold = 1.0f; p.follow = true; p.layer = 6; p.scan = knobForSpeed (20.0f);
        process (e, p, silence (5.0));                  // 5 s at 20x = 100 s of material, 60 s of it in Omega
        CHECK (e.playheadSeconds() >= 0.0 && e.playheadSeconds() < 60.0, "Omega playhead stays inside the 60 s: %f", e.playheadSeconds());
    }
}

// ---------------------------------------------------------------------------
// Milestone 5
// ---------------------------------------------------------------------------

// Noise bursts (fast attack, decaying) at the given times, on a very quiet floor.
static std::vector<float> bursts (double seconds, const std::vector<double>& times, float amp)
{
    std::vector<float> v = silence (seconds);
    uint32_t s = 24680u;
    auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.0f * 2.0f - 1.0f; };
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = 0.0005f * rnd();
    for (double t : times)
    {
        const size_t from = (size_t) (t * kSr);
        const size_t len  = (size_t) (0.25 * kSr);
        for (size_t i = 0; i < len && from + i < v.size(); ++i)
        {
            const float env = std::exp (-(float) i / (0.05f * (float) kSr));
            const float fadeOut = i + 200 > len ? (float) (len - i) / 200.0f : 1.0f;
            v[from + i] += amp * env * fadeOut * rnd();
        }
    }
    return v;
}

// Runs a signal through the engine and reports when recording started and stopped.
struct RecTimes { double start = -1.0, stop = -1.0; };
static RecTimes processWatching (ceo::Engine& e, const ceo::Params& p, const std::vector<float>& in)
{
    RecTimes t;
    size_t pos = 0;
    while (pos < in.size())
    {
        const int n = (int) std::min<size_t> (kBlock, in.size() - pos);
        std::vector<float> a (in.begin() + (long) pos, in.begin() + (long) pos + n), b = a;
        float* ch[2] = { a.data(), b.data() };
        e.setParams (p);
        e.process (ch, 2, n);
        if (e.isRecording() && t.start < 0.0) t.start = (double) pos / kSr;
        if (t.start >= 0.0 && ! e.isRecording() && t.stop < 0.0) t.stop = (double) pos / kSr;
        pos += (size_t) n;
    }
    return t;
}

// Different material on the two input channels.
static void processTwo (ceo::Engine& e, const ceo::Params& p, std::vector<float> a, std::vector<float> b)
{
    for (size_t pos = 0; pos < a.size(); pos += kBlock)
    {
        const int n = (int) std::min<size_t> (kBlock, a.size() - pos);
        float* ch[2] = { a.data() + pos, b.data() + pos };
        e.setParams (p);
        e.process (ch, 2, n);
    }
}

static void testOnsetCapture()
{
    using namespace ceo::map;
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f;
        p.onsetCapture = true;
        p.sense = 0.6f;
        p.hold = 0.3f;
        const float holdLen = holdSeconds (0.3f);

        process (e, p, bursts (1.0, {}, 0.5f));
        CHECK (e.onsetCount() == 0 && ! e.isRecording(), "quiet floor must not trigger");
        CHECK (e.onsetArmed(), "should be armed");

        const RecTimes t = processWatching (e, p, bursts (2.0, { 0.3 }, 0.5f));
        CHECK (e.onsetCount() == 1, "one burst = one onset (count %d)", e.onsetCount());
        CHECK (t.start > 0.29 && t.start < 0.36, "recording started at %f", t.start);
        CHECK (t.stop > 0, "recording must stop by itself");
        CHECK (std::fabs ((t.stop - t.start) - holdLen) < 0.1, "recorded %f s, hold is %f s", t.stop - t.start, holdLen);
        CHECK (e.layerHasContent (0), "layer alpha should now hold the burst");

        const float* a = e.layerAudio (0, 0);
        CHECK (a != nullptr, "layer audio");
        if (a != nullptr)
        {
            double firstLoud = -1.0;
            for (int i = 0; i < (int) (0.2 * kSr); ++i)
                if (std::fabs (a[i]) > 0.05f) { firstLoud = (double) i / kSr; break; }
            CHECK (firstLoud >= 0.0 && firstLoud < 0.06, "attack should sit at the start of the layer (%f)", firstLoud);
        }
    }

    {   // Hold also debounces.
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f; p.onsetCapture = true; p.sense = 0.6f; p.hold = 0.6f;
        process (e, p, bursts (3.0, { 0.3, 1.2, 2.0 }, 0.5f));
        CHECK (e.onsetCount() == 1, "bursts inside the hold time must be ignored (count %d)", e.onsetCount());
    }

    {   // Short hold: separate bursts each start a recording.
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f; p.onsetCapture = true; p.sense = 0.6f; p.hold = 0.15f;
        process (e, p, bursts (4.0, { 0.3, 1.2, 2.2, 3.1 }, 0.5f));
        CHECK (e.onsetCount() == 4, "four separate bursts = four onsets (count %d)", e.onsetCount());
        CHECK (e.captureRanOutCount() == 0, "onset recordings ending do not count as the Capture switch running out (%u)", (unsigned) e.captureRanOutCount());
    }

    {   // Stereo input mode: off.
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f; p.onsetCapture = true; p.sense = 0.6f; p.stereoInput = true;
        process (e, p, bursts (1.5, { 0.3 }, 0.5f));
        CHECK (e.onsetCount() == 0 && ! e.isRecording(), "no onset capture in Stereo input mode");
    }

    {   // Switch off: nothing.
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f; p.sense = 0.6f;
        process (e, p, bursts (1.5, { 0.3 }, 0.5f));
        CHECK (e.onsetCount() == 0, "off means off");
    }

    {   // Sense: turning it up never catches less, and somewhere between soft and loud there is a burst only high Sense catches.
        bool gap = false, never_less = true;
        for (float amp : { 0.1f, 0.05f, 0.02f, 0.01f, 0.005f, 0.0025f, 0.0015f, 0.001f })
        {
            ceo::Engine lo, hi;
            lo.prepare (kSr, kBlock, 2); hi.prepare (kSr, kBlock, 2);
            ceo::Params p;
            p.dryWet = 0.0f; p.onsetCapture = true; p.hold = 0.15f;
            auto in = bursts (1.5, { 0.4 }, amp);
            p.sense = 0.0f; process (lo, p, in);
            p.sense = 1.0f; process (hi, p, in);
            if (hi.onsetCount() < lo.onsetCount()) never_less = false;
            if (lo.onsetCount() == 0 && hi.onsetCount() >= 1) gap = true;
        }
        CHECK (never_less, "high Sense must catch at least what low Sense catches");
        CHECK (gap, "some soft burst should be caught only with Sense high");
    }
}

static void testStereoInput()
{
    auto left  = sine (0.5, 300.0, 0.5f);
    auto right = sine (0.5, 700.0, 0.5f);

    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    ceo::Params p;
    p.dryWet = 0.0f; p.stereoInput = true; p.sense = 0.5f;
    process (e, p, silence (0.02));
    p.capture = true;
    processTwo (e, p, left, right);
    p.capture = false;
    process (e, p, silence (0.05));
    CHECK (e.layerHasContent (0) && e.layerIsStereo (0), "stereo recording expected");
    const float* l = e.layerAudio (0, 0);
    const float* r = e.layerAudio (0, 1);
    double diff = 0.0, lp = 0.0, rp = 0.0;
    for (int i = 4800; i < 24000; ++i) { diff += std::fabs (l[i] - r[i]); lp += l[i] * l[i]; rp += r[i] * r[i]; }
    CHECK (diff > 100.0 && lp > 100.0 && rp > 100.0, "left and right should differ (diff %f)", diff);

    ceo::Engine m;
    m.prepare (kSr, kBlock, 2);
    record (m, left);
    CHECK (m.layerHasContent (0) && ! m.layerIsStereo (0), "mono recording expected");

    // Sense at the bottom = right-hand input silent.
    ceo::Engine q;
    q.prepare (kSr, kBlock, 2);
    ceo::Params s;
    s.dryWet = 0.0f; s.stereoInput = true; s.sense = 0.0f;
    process (q, s, silence (0.02));
    s.capture = true;
    processTwo (q, s, left, right);
    s.capture = false;
    process (q, s, silence (0.05));
    const float* qr = q.layerAudio (0, 1);
    double qp = 0.0;
    for (int i = 4800; i < 24000; ++i) qp += qr[i] * qr[i];
    CHECK (qp < 1e-6, "Sense at the bottom silences the right-hand input (%f)", qp);

    // A stereo layer with sound only on the left: playback with no Mod keeps it left-heavy.
    ceo::Engine g;
    g.prepare (kSr, kBlock, 2);
    ceo::Params gp;
    gp.dryWet = 0.0f; gp.stereoInput = true; gp.sense = 0.5f;
    process (g, gp, silence (0.02));
    gp.capture = true;
    processTwo (g, gp, left, silence (0.5));
    gp.capture = false;
    process (g, gp, silence (0.05));
    g.reset();
    gp.dryWet = 1.0f; gp.length = 0.3f; gp.modAmount = 1.0f; gp.modTarget = ceo::kModPan;
    auto out = process (g, gp, silence (1.0));
    CHECK (rms (out.L, 12000, 48000) > 0.005f && rms (out.R, 12000, 48000) > 0.005f, "folded to mono: both sides carry sound (%f %f)",
           rms (out.L, 12000, 48000), rms (out.R, 12000, 48000));
}

static void testModSection()
{
    auto src = sine (1.0, 330.0, 0.4f);
    auto render = [&] (int target, float amount, bool silent)
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, src);
        ceo::Params p;
        p.dryWet = 0.6f; p.length = 0.3f; p.modTarget = target; p.modAmount = amount;
        return process (e, p, silent ? silence (3.0) : src);
    };

    {   // Centre = off.
        auto a = render (ceo::kModReverb, 0.5f, true);
        auto b = render (ceo::kModDelay, 0.5f, true);
        bool same = true;
        for (size_t i = 0; i < a.L.size(); ++i) if (a.L[i] != b.L[i] || a.R[i] != b.R[i]) { same = false; break; }
        CHECK (same, "Mod at centre must change nothing");
    }

    for (int t = 0; t < 4; ++t)
        for (float amt : { 0.0f, 0.1f, 0.3f, 0.52f, 0.7f, 0.95f, 1.0f })
        {
            auto o = render (t, amt, false);
            CHECK (allFiniteAndBounded (o, 8.0f), "target %d amount %f unstable", t, amt);
        }

    {   // Delay up: echoes.
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f; p.modTarget = ceo::kModDelay; p.modAmount = 0.8f;
        auto in = sine (0.3, 440.0, 0.5f);
        in.resize ((size_t) (3.0 * kSr), 0.0f);
        auto o = process (e, p, in);
        CHECK (rms (o.L, (size_t) (0.5 * kSr), (size_t) (2.0 * kSr)) > 0.005f, "delay up should echo after the sound stops");
    }

    {   // Delay down: rings and stays stable.
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f; p.modTarget = ceo::kModDelay; p.modAmount = 0.2f;
        auto in = silence (3.0);
        for (int i = 0; i < 200; ++i) in[(size_t) i] = 0.5f;
        auto o = process (e, p, in);
        CHECK (allFiniteAndBounded (o, 4.0f), "metallic delay stable");
        CHECK (rms (o.L, (size_t) (0.3 * kSr), (size_t) (0.6 * kSr)) > 0.001f, "metallic delay should ring");
    }

    {   // Reverb up: longer tail with more Mod.
        float tail[2];
        const float amts[2] = { 0.7f, 0.9f };
        for (int k = 0; k < 2; ++k)
        {
            ceo::Engine e;
            e.prepare (kSr, kBlock, 2);
            ceo::Params p;
            p.dryWet = 0.0f; p.modTarget = ceo::kModReverb; p.modAmount = amts[k];
            auto in = sine (0.3, 440.0, 0.5f);
            in.resize ((size_t) (6.0 * kSr), 0.0f);
            auto o = process (e, p, in);
            tail[k] = rms (o.L, (size_t) (4.0 * kSr), (size_t) (5.5 * kSr));
        }
        CHECK (tail[0] > 0.0f && tail[1] > tail[0], "reverb tail should be longer with more Mod (%f %f)", tail[0], tail[1]);
    }

    {   // Freeze and direct-sound removal.
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f; p.modTarget = ceo::kModReverb; p.modAmount = 1.0f;
        auto in = sine (0.5, 440.0, 0.5f);
        in.resize ((size_t) (20.0 * kSr), 0.0f);
        auto o = process (e, p, in);
        const float a = rms (o.L, (size_t) (3.0 * kSr), (size_t) (5.0 * kSr));
        const float b = rms (o.L, (size_t) (17.0 * kSr), (size_t) (19.0 * kSr));
        CHECK (a > 0.005f && b > 0.8f * a && b < 1.3f * a, "frozen reverb should hold steady (%f then %f)", a, b);

        ceo::Engine d;
        d.prepare (kSr, kBlock, 2);
        ceo::Params q;
        q.dryWet = 0.0f; q.modTarget = ceo::kModReverb; q.modAmount = 0.0f;
        auto o2 = process (d, q, sine (1.0, 440.0, 0.5f));
        CHECK (rms (o2.L, 24000, 48000) < 0.5f * rms (sine (1.0, 440.0, 0.5f), 24000, 48000), "bottom: direct sound is mostly gone");
    }

    {   // Hold target.
        double len[2];
        const float amts[2] = { 0.5f, 1.0f };
        for (int k = 0; k < 2; ++k)
        {
            ceo::Engine e;
            e.prepare (kSr, kBlock, 2);
            ceo::Params p;
            p.dryWet = 0.0f; p.onsetCapture = true; p.sense = 0.6f; p.hold = 0.2f;
            p.modTarget = ceo::kModHold; p.modAmount = amts[k];
            const RecTimes t = processWatching (e, p, bursts (5.0, { 0.3 }, 0.5f));
            len[k] = t.stop - t.start;
        }
        CHECK (len[0] > 0 && len[1] > len[0] + 0.3, "Mod on Hold lengthens the recording (%f vs %f)", len[0], len[1]);
    }

    {   // Pan down: sound on both sides over time.
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, src);
        ceo::Params p;
        p.dryWet = 1.0f; p.length = 0.3f; p.modTarget = ceo::kModPan; p.modAmount = 0.0f;
        auto o = process (e, p, silence (3.0));
        double lo = 0, ro = 0;
        for (size_t i = 24000; i < o.L.size(); ++i) { lo += (double) o.L[i] * o.L[i]; ro += (double) o.R[i] * o.R[i]; }
        CHECK (lo > 0 && ro > 0, "both sides get sound with coin-toss panning");
    }
}

static void testWavetable()
{
    auto tone = sine (1.0, 300.0, 0.5f);

    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);
    record (e, tone);

    ceo::Params p;
    p.dryWet = 1.0f; p.length = 0.0f; p.intensity = 0.7f; p.pitch = 0.5f; p.pitchDeviation = 0.5f;
    p.scan = 0.03f; p.spray = 0.0f;
    auto o = process (e, p, silence (2.0));
    CHECK (e.wavetableActive(), "oscillator should be sounding");
    CHECK (rms (o.L, 48000, 96000) > 0.02f, "oscillator audible (%f)", rms (o.L, 48000, 96000));
    const double p0  = tonePower (o.L, 48000, 96000, 130.8128);
    const double p13 = tonePower (o.L, 48000, 96000, 13.0 * 130.8128);
    CHECK (p13 > 20.0 * std::max (p0, 1e-9), "tone colour should come out as a harmonic (fundamental %g, 13th %g)", p0, p13);

    ceo::Engine e2;
    e2.prepare (kSr, kBlock, 2);
    record (e2, tone);
    p.pitch = 0.75f;
    auto o2 = process (e2, p, silence (2.0));
    const double up = tonePower (o2.L, 48000, 96000, 13.0 * 261.6256);
    CHECK (up > 20.0 * std::max (tonePower (o2.L, 48000, 96000, 13.0 * 130.8128), 1e-9), "octave up (%g)", up);

    p.pitch = 0.5f;
    p.intensity = 0.0f;
    auto o3 = process (e, p, silence (2.0));
    CHECK (rms (o3.L, 72000, 96000) < 0.001f, "intensity at bottom silences the oscillator (%f)", rms (o3.L, 72000, 96000));
    CHECK (! e.wavetableActive(), "oscillator inactive");

    ceo::Params s = p;
    s.intensity = 0.0f;
    process (e, s, silence (0.1));
    s.strike = ! s.strike;
    auto hit = process (e, s, silence (0.1));
    CHECK (e.wavetableActive(), "Strike should start a note");
    CHECK (rms (hit.L, 0, 4800) > 0.001f, "note audible (%f)", rms (hit.L, 0, 4800));
    auto after = process (e, s, silence (1.5));
    CHECK (! e.wavetableActive(), "note should finish");
    CHECK (rms (after.L, 48000, 72000) < 1e-4f, "silent after the note");

    ceo::Params g = p;
    g.length = 0.3f; g.intensity = 0.6f;
    auto og = process (e, g, silence (1.0));
    CHECK (! e.wavetableActive(), "no oscillator with normal grains");
    CHECK (rms (og.L, 24000, 48000) > 0.01f, "grains still play");

    uint32_t sd = 13579u;
    auto rnd = [&sd]() { sd = sd * 1664525u + 1013904223u; return (float) (sd >> 8) / 16777216.0f; };
    for (int round = 0; round < 20; ++round)
    {
        ceo::Engine f;
        f.prepare (kSr, 512, 2);
        record (f, round & 1 ? tone : bursts (1.0, { 0.1, 0.5 }, 0.5f));
        for (int step = 0; step < 6; ++step)
        {
            ceo::Params w;
            w.dryWet = 1.0f; w.length = rnd() * 0.08f; w.intensity = rnd(); w.pitch = rnd(); w.pitchDeviation = rnd();
            w.scan = rnd(); w.spray = rnd(); w.grainDirection = rnd(); w.grainWindow = rnd(); w.strike = rnd() > 0.5f;
            w.modTarget = (int) (rnd() * 3.99f); w.modAmount = rnd();
            auto r = process (f, w, sine (0.4, 200.0 + 800.0 * rnd(), 0.5f));
            CHECK (allFiniteAndBounded (r, 8.0f), "wavetable sweep round %d step %d", round, step);
        }
    }
}

static void testMilestone5Mapping()
{
    using namespace ceo::map;
    CHECK (std::fabs (holdSeconds (0.5f) - 2.5f) < 1e-4f && std::fabs (holdSeconds (1.0f) - 10.0f) < 1e-3f && std::fabs (holdSeconds (0.0f) - 0.005f) < 1e-5f, "hold ends");
    for (float k = 0.0f; k < 0.99f; k += 0.01f)
        CHECK (holdSeconds (k + 0.01f) > holdSeconds (k), "hold not rising at %f", k);
    CHECK (modBipolar (0.5f) == 0.0f && modBipolar (0.51f) == 0.0f, "Mod flat spot");
    CHECK (std::fabs (modBipolar (1.0f) - 1.0f) < 1e-5f && std::fabs (modBipolar (0.0f) + 1.0f) < 1e-5f, "Mod ends");
    CHECK (onsetSensitivity (1.0f).ratio < onsetSensitivity (0.0f).ratio && onsetSensitivity (1.0f).floorFlux < onsetSensitivity (0.0f).floorFlux, "Sense up = more sensitive");
    CHECK (wavetableMix (0.0f) == 1.0f && wavetableMix (0.06f) == 0.0f && wavetableMix (0.5f) == 0.0f, "wavetable crossfade ends");
    for (float k = 0.0f; k < 0.06f; k += 0.002f)
        CHECK (wavetableMix (k + 0.002f) <= wavetableMix (k), "crossfade monotone at %f", k);
    CHECK (std::fabs (wavetableMorphSeconds (0.0f) - 0.01f) < 1e-5f && wavetableMorphSeconds (1.0f) > 2.9f, "morph time ends");
    CHECK (! reverbMap (0.0f).active && ! delayMap (0.0f).active, "no effect at centre");
    CHECK (reverbMap (1.0f).freeze && reverbMap (-1.0f).freeze && ! reverbMap (0.5f).freeze, "freeze only at the extremes");
    CHECK (delayMap (-0.5f).metallic && ! delayMap (0.5f).metallic, "metallic only below centre");
    for (int i = 0; i < 100; ++i)
    {
        const float u1 = (float) i / 99.0f;
        for (float d = -1.0f; d <= 1.0f; d += 0.1f)
        {
            const auto ps = panSettings (d, u1, 0.3f);
            CHECK (ps.angle >= -1e-5f && ps.angle <= 1.5708f + 1e-4f && ps.monoBlend >= 0.0f && ps.monoBlend <= 1.0f, "pan out of range");
        }
    }
}

// ---------------------------------------------------------------------------
// Milestone 6: restarting playback
// ---------------------------------------------------------------------------

// Logic can prepare the plugin again with the same settings when playback restarts. Sound that is already
// playing (grains, the oscillator, reverb and echo tails) must carry on without an audible jump.
// The test restarts several times in the middle of a steady sound and compares the jump across the
// restart with the largest ordinary step between neighbouring samples just before it.
// The reverb must not get louder (and start to distort) as the decay is lengthened with Mod amount.
static void testReverbLevelStaysEven()
{
    float level[3];
    const float amts[3] = { 0.6f, 0.75f, 0.95f };
    for (int k = 0; k < 3; ++k)
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, sine (4.0, 220.0, 0.4f));
        ceo::Params p;
        p.dryWet = 1.0f; p.length = 0.5f; p.intensity = 0.8f; p.spray = 0.3f; p.scan = 0.3f;
        p.modTarget = ceo::kModReverb; p.modAmount = amts[k];
        auto out = process (e, p, silence (16.0));
        level[k] = rms (out.L, (size_t) (8.0 * kSr), out.L.size());
        float peak = 0.0f;
        for (size_t i = (size_t) (8.0 * kSr); i < out.L.size(); ++i)
            peak = std::max (peak, std::fabs (out.L[i]));
        CHECK (peak <= 1.0f, "reverb output stays within +-1 at Mod %f (peak %f)", amts[k], peak);
    }
    CHECK (level[2] < 1.4f * level[0] && level[1] < 1.4f * level[0], "reverb level stays even with Mod (%f %f %f)", level[0], level[1], level[2]);
}

static void testRestartIsGentle()
{
    for (int variant = 0; variant < 4; ++variant)
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        record (e, sine (4.0, 220.0, 0.5f));

        ceo::Params p;
        p.dryWet = 1.0f; p.length = 0.55f; p.intensity = 0.5f; p.spray = 0.4f;   // grains from all over the recording: a steady sound
        if (variant == 1) { p.modTarget = ceo::kModReverb; p.modAmount = 0.85f; }
        if (variant == 2) { p.modTarget = ceo::kModDelay;  p.modAmount = 0.85f; }
        if (variant == 3) { p.length = 0.0f; }           // Wavetable mode: the oscillator is the sound

        auto run = [&] (int blocks) { return process (e, p, std::vector<float> ((size_t) blocks * kBlock, 0.0f)); };

        run (400);                                       // about 2 s, so the sound is fully going

        float worst = 0.0f, loudest = 0.0f;
        for (int r = 0; r < 6; ++r)
        {
            auto before = run (10 + r);                  // a different place in the sound each time
            e.prepare (kSr, kBlock, 2);                  // what Logic does when playback restarts
            auto after = run (8);

            const size_t n = before.L.size();
            float natural = 0.0f;
            for (size_t i = n - 2400; i + 1 < n; ++i)
                natural = std::max (natural, std::max (std::fabs (before.L[i + 1] - before.L[i]), std::fabs (before.R[i + 1] - before.R[i])));
            // The host's output is silent while stopped, so what the plugin gives after the restart is judged
            // as a start from zero: it must begin at (almost) zero and never step harder than the sound itself.
            float jump = std::max (std::fabs (after.L[0]), std::fabs (after.R[0]));
            for (size_t i = 1; i < 1500; ++i)
                jump = std::max (jump, std::max (std::fabs (after.L[i] - after.L[i - 1]), std::fabs (after.R[i] - after.R[i - 1])));
            CHECK (rms (after.L, 1000, 1500) > 0.3f * rms (before.L, n - 2400, n), "variant %d: the sound is back at full level after the fade-in", variant);
            worst = std::max (worst, jump / std::max (natural, 1e-4f));
            loudest = std::max (loudest, rms (before.L, n - 2400, n));
        }
        std::printf ("restart test, variant %d: worst jump = %.2f x the normal sample step (limit 3)\n", variant, worst);
        CHECK (loudest > 0.01f, "variant %d: there was sound to cut (%f)", variant, loudest);
        CHECK (worst < 3.0f, "variant %d: a restart makes a jump %.1f times the normal sample step", variant, worst);
        CHECK (e.layerHasContent (0), "variant %d: the recording survives a restart", variant);
    }

    // A restart while recording stops the recording and keeps what was recorded so far.
    {
        ceo::Engine e;
        e.prepare (kSr, kBlock, 2);
        ceo::Params p;
        p.dryWet = 0.0f;
        process (e, p, silence (0.02));
        p.capture = true;
        process (e, p, sine (0.5, 220.0, 0.5f));
        CHECK (e.isRecording(), "recording is going");
        e.prepare (kSr, kBlock, 2);
        CHECK (! e.isRecording(), "a restart stops recording");
        CHECK (e.layerHasContent (0), "what was recorded so far is kept");
    }
}

// Information for the stream display: recent grain starts, the recording counter and a layer's loudness shape.
static void testDisplayInfo()
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);

    // Recording counter: goes up when recording and when erasing.
    const uint32_t rev0 = e.recordingRevision();
    float env[40];
    CHECK (e.layerEnvelope (0, env, 40), "an empty layer can be read");
    float emptySum = 0.0f;
    for (float v : env) emptySum += v;
    CHECK (emptySum == 0.0f, "an empty layer has a flat envelope");
    CHECK (! e.layerEnvelope (6, env, 40) && ! e.layerEnvelope (-1, env, 40), "only layers 0 to 5 can be read");

    record (e, sine (1.0, 220.0, 0.5f));                 // 1 s of sine at the start of layer alpha
    CHECK (e.recordingRevision() != rev0, "recording changes the counter");

    // Loudness shape: 40 points over 10 s = 0.25 s each. The first four carry the sine (RMS 0.5/sqrt 2), the rest are silent.
    CHECK (e.layerEnvelope (0, env, 40), "a recorded layer can be read");
    for (int i = 0; i < 3; ++i)
        CHECK (std::fabs (env[i] - 0.3536f) < 0.02f, "envelope point %d carries the sine (%f)", i, env[i]);
    for (int i = 6; i < 40; ++i)
        CHECK (env[i] < 0.005f, "envelope point %d is silent (%f)", i, env[i]);
    CHECK (e.layerEnvelope (1, env, 40) && env[0] == 0.0f, "another layer is still empty");

    const uint32_t rev1 = e.recordingRevision();
    ceo::Params p;
    p.dryWet = 1.0f;
    process (e, p, silence (0.02));
    click (e, p, p.erase);
    CHECK (e.recordingRevision() != rev1, "Erase changes the counter");
    CHECK (e.layerEnvelope (0, env, 40) && env[0] == 0.0f, "Erase empties the envelope");

    // Grain starts.
    ceo::Engine g;
    g.prepare (kSr, kBlock, 2);
    record (g, sine (3.0, 220.0, 0.2f));
    uint32_t cursor = 0;
    ceo::GrainStart gs[200];
    g.takeGrainStarts (cursor, gs, 200);                 // grains that started while recording are not interesting
    CHECK (g.takeGrainStarts (cursor, gs, 200) == 0, "nothing new since the last call");

    ceo::Params q;
    q.dryWet = 1.0f; q.length = 0.3f; q.scan = 0.2f; q.spray = 0.0f; q.grainDirection = 0.0f;
    process (g, q, silence (0.6));
    int n = g.takeGrainStarts (cursor, gs, 200);
    CHECK (n > 5 && n <= 128, "continuous grains are reported (%d)", n);
    bool allAtScan = true, anyStrike = false, anyReversed = false;
    for (int i = 0; i < n; ++i)
    {
        if (std::fabs (gs[i].position - 0.2f) > 0.005f) allAtScan = false;
        anyStrike   = anyStrike   || gs[i].strike;
        anyReversed = anyReversed || gs[i].reversed;
    }
    CHECK (allAtScan, "with no Spray every grain starts at the Scan position");
    CHECK (! anyStrike && ! anyReversed, "continuous forward grains are not marked as Strike or reversed");
    CHECK (g.takeGrainStarts (cursor, gs, 200) == 0, "nothing new is reported twice");

    // Backwards grains and Spray.
    q.grainDirection = 1.0f; q.spray = 1.0f;
    process (g, q, silence (0.6));
    n = g.takeGrainStarts (cursor, gs, 200);
    bool allReversed = n > 5;
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        allReversed = allReversed && gs[i].reversed;
        lo = std::min (lo, gs[i].position);
        hi = std::max (hi, gs[i].position);
    }
    CHECK (allReversed, "Grain direction at the end: every grain is reported as reversed");
    CHECK (hi - lo > 0.3f && lo >= 0.0f && hi <= 1.0f, "full Spray spreads the starts over the layer (%f to %f)", lo, hi);

    // Strike only (continuous engine off): one click, one reported start, marked as Strike.
    ceo::Engine k;
    k.prepare (kSr, kBlock, 2);
    record (k, sine (3.0, 220.0, 0.2f));
    ceo::Params r;
    r.dryWet = 1.0f; r.intensity = 0.0f; r.length = 0.4f; r.scan = 0.05f;
    process (k, r, silence (0.1));
    uint32_t kc = 0;
    k.takeGrainStarts (kc, gs, 200);                     // (drain what started while recording)
    process (k, r, silence (0.1));
    CHECK (k.takeGrainStarts (kc, gs, 200) == 0, "continuous engine off: nothing starts by itself");
    click (k, r, r.strike);
    n = k.takeGrainStarts (kc, gs, 200);
    CHECK (n == 1 && gs[0].strike && std::fabs (gs[0].position - 0.05f) < 0.005f, "one Strike click, one reported Strike start (%d)", n);

    // More starts than the engine remembers: only the newest 128 come back, and never more than asked for.
    ceo::Engine m;
    m.prepare (kSr, kBlock, 2);
    record (m, sine (3.0, 220.0, 0.2f));
    ceo::Params t;
    t.dryWet = 1.0f; t.length = 0.002f; t.intensity = 0.5f;
    process (m, t, silence (2.0));
    uint32_t mc = 0;
    n = m.takeGrainStarts (mc, gs, 200);
    CHECK (n == 128, "at most the last 128 starts are kept (%d)", n);
    process (m, t, silence (0.5));
    CHECK (m.takeGrainStarts (mc, gs, 10) <= 10, "never more than asked for");

    // In Omega the position runs along all six layers: Scan 0.08 is 4.8 s of 60 s, inside layer alpha.
    ceo::Engine o;
    o.prepare (kSr, kBlock, 2);
    record (o, sine (3.0, 220.0, 0.2f));
    ceo::Params u;
    u.dryWet = 1.0f; u.layer = 6; u.scan = 0.01f; u.length = 0.3f;
    process (o, u, silence (0.6));
    uint32_t oc = 0;
    n = o.takeGrainStarts (oc, gs, 200);
    bool allThere = n > 3;
    for (int i = 0; i < n; ++i)
        if (std::fabs (gs[i].position - 0.01f) > 0.005f) allThere = false;
    CHECK (allThere, "in Omega the start position is along all six layers (%d starts)", n);
}

// The plugin window reads the layers for the display while audio runs. When the host changes the sample rate the
// engine replaces the layer memory; the window's reading must never touch memory that has just gone.
// (Run with the address sanitizer, a failure here is reported as a memory error.)
static void testDisplayWhileReallocating()
{
    ceo::Engine e;
    e.prepare (kSr, kBlock, 2);

    std::atomic<bool> stop { false };
    std::atomic<int>  reads { 0 };
    std::thread reader ([&]
    {
        float env[72];
        ceo::GrainStart gs[64];
        uint32_t cursor = 0;
        while (! stop.load())
        {
            if (e.layerEnvelope (0, env, 72))
                ++reads;
            e.takeGrainStarts (cursor, gs, 64);
            (void) e.recordingRevision();
        }
    });

    for (int i = 0; i < 40; ++i)
    {
        e.prepare ((i % 2) ? 44100.0 : 48000.0, kBlock, 2);          // new memory each time
        ceo::Params p;
        p.dryWet = 0.0f;
        process (e, p, silence (0.01));
        p.capture = true;
        process (e, p, sine (0.1, 220.0, 0.5f));                     // layer alpha now has content: the reader really reads it
        p.capture = false;
        process (e, p, silence (0.01));
    }

    stop = true;
    reader.join();
    CHECK (reads.load() > 0, "the display could read the layer while the engine ran (%d reads)", reads.load());
}

int main()
{
    testMapping();
    testWindow();
    testDryPassThrough();
    testSilentWhenEmpty();
    testRecording();
    testGrainsMakeSound();
    testLoudnessIsRoughlyConsistent();
    testIntensity();
    testDirection();
    testSprayAndScanStayInsideLayer();
    testMono();
    testFuzz();
    testResetKeepsRecording();
    testLayersAndOmega();
    testDubUndoErase();
    testReverbLevelStaysEven();
    testStrike();
    testPitchDeviation();
    testFollow();
    testMilestone5Mapping();
    testOnsetCapture();
    testStereoInput();
    testModSection();
    testWavetable();
    testRestartIsGentle();
    testDisplayInfo();
    testDisplayWhileReallocating();

    if (failures == 0)
        std::printf ("engine tests: all checks OK\n");
    else
        std::printf ("engine tests: %d FAILED\n", failures);

    return failures == 0 ? 0 : 1;
}
