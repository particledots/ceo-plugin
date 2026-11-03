// Self-test for the Ceo engine. Run automatically by the build; the build fails if any check fails.

#include "ceo/Engine.h"
#include "ceo/Mapping.h"
#include "ceo/Window.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
        p.dryWet = 1.0f; p.follow = true; p.scan = scanKnob; p.length = 0.3f; p.grainWindow = 0.0f; p.intensity = 0.2f;
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
        p.dryWet = 1.0f; p.follow = true; p.scan = knobForSpeed (1.0f); p.length = 0.3f; p.grainWindow = 0.0f; p.intensity = 0.2f;
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
        p.dryWet = 1.0f; p.follow = true; p.scan = knobForSpeed (1.0f);
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
        p.dryWet = 1.0f; p.follow = true; p.layer = 6; p.scan = knobForSpeed (20.0f);
        process (e, p, silence (5.0));                  // 5 s at 20x = 100 s of material, 60 s of it in Omega
        CHECK (e.playheadSeconds() >= 0.0 && e.playheadSeconds() < 60.0, "Omega playhead stays inside the 60 s: %f", e.playheadSeconds());
    }
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
    testStrike();
    testPitchDeviation();
    testFollow();

    if (failures == 0)
        std::printf ("engine tests: all checks OK\n");
    else
        std::printf ("engine tests: %d FAILED\n", failures);

    return failures == 0 ? 0 : 1;
}
