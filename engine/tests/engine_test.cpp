// Self-test for the engine: checks that audio comes out bit-for-bit identical
// to what went in, whatever the control values are. Run automatically by the build.

#include "ceo/Engine.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static bool runCase (int numChannels, int numSamples, const ceo::Params& p)
{
    ceo::Engine engine;
    engine.prepare (48000.0, numSamples, numChannels);
    engine.setParams (p);

    std::vector<std::vector<float>> original (numChannels), working (numChannels);
    std::vector<float*> pointers (numChannels);

    for (int c = 0; c < numChannels; ++c)
    {
        original[c].resize (numSamples);
        for (int i = 0; i < numSamples; ++i)
            original[c][i] = 0.9f * std::sin (0.01f * (float) i * (float) (c + 1))
                             + 0.05f * (float) ((i * 7 + c) % 13) / 13.0f;
        working[c] = original[c];
        pointers[c] = working[c].data();
    }

    engine.process (pointers.data(), numChannels, numSamples);

    for (int c = 0; c < numChannels; ++c)
        if (std::memcmp (original[c].data(), working[c].data(), sizeof (float) * (size_t) numSamples) != 0)
            return false;

    return true;
}

int main()
{
    ceo::Params defaults;

    ceo::Params extreme;
    extreme.inputLevel = 1.0f;  extreme.dryWet = 0.0f;  extreme.outputLevel = 0.0f;
    extreme.capture = true;     extreme.strike = true;  extreme.layer = 6;
    extreme.intensity = 1.0f;   extreme.length = 0.0f;  extreme.spray = 1.0f;

    int failures = 0;
    for (int channels : { 1, 2 })
        for (int samples : { 1, 64, 512, 4096 })
        {
            if (! runCase (channels, samples, defaults)) { std::printf ("FAIL defaults  ch=%d n=%d\n", channels, samples); ++failures; }
            if (! runCase (channels, samples, extreme))  { std::printf ("FAIL extreme   ch=%d n=%d\n", channels, samples); ++failures; }
        }

    if (failures == 0)
        std::printf ("engine pass-through test: all cases OK\n");

    return failures == 0 ? 0 : 1;
}
