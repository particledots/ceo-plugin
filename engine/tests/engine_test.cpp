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

// Records 'in' into layer alpha, then switches Capture off.
static void record (ceo::Engine& e, const std::vector<float>& in)
{
    ceo::Params p;
    p.dryWet = 0.0f;                        // dry only while recording
    process (e, p, silence (0.02));         // a first block with Capture off, then press it
    p.capture = true;
    process (e, p, in);
    p.capture = false;
    process (e, p, silence (0.05));
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

    // Recording into a layer that is not alpha does nothing in milestone 3.
    ceo::Engine e2;
    e2.prepare (kSr, kBlock, 2);
    ceo::Params q;
    q.layer = 2;
    q.capture = true;
    process (e2, q, sine (0.1, 440.0, 0.5f));
    CHECK (! e2.isRecording() && ! e2.layerHasContent(), "layers beyond alpha are milestone 4");
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

    if (failures == 0)
        std::printf ("engine tests: all checks OK\n");
    else
        std::printf ("engine tests: %d FAILED\n", failures);

    return failures == 0 ? 0 : 1;
}
