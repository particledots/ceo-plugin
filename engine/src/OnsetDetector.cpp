#include "ceo/OnsetDetector.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ceo
{
namespace
{
constexpr int   kHistory     = 64;       // frames of recent change kept for the average
constexpr int   kWarmUp      = 6;        // frames to listen before the first onset can be reported
constexpr float kLogScale    = 2000.0f;  // sets the noise floor of the analysis (about -66 dB per band)
constexpr float kRefractory  = 0.060f;   // seconds after an onset during which the detector reports nothing more
}

void OnsetDetector::prepare (double sr)
{
    // The same time span at any sample rate: about 21 ms frames every 5 ms.
    sampleRate = sr > 0.0 ? sr : 48000.0;
    frame = sampleRate > 56000.0 ? 2048 : 1024;
    hop   = frame / 4;

    fft = std::make_unique<Fft> (frame);
    ring.assign ((size_t) frame, 0.0f);
    window.resize ((size_t) frame);
    for (int i = 0; i < frame; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * 3.14159265f * (float) i / (float) frame);
    re.assign ((size_t) frame, 0.0f);
    im.assign ((size_t) frame, 0.0f);
    prevLog.assign ((size_t) frame / 2, 0.0f);
    curLog.assign ((size_t) frame / 2, 0.0f);
    history.assign ((size_t) kHistory, 0.0f);

    reset();
}

void OnsetDetector::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (prevLog.begin(), prevLog.end(), 0.0f);
    std::fill (curLog.begin(), curLog.end(), 0.0f);
    std::fill (history.begin(), history.end(), 0.0f);
    ringPos = sinceHop = frames = histCount = histPos = 0;
    refractory = 0;
    prevAbove = false;
    lastFlux = lastThr = 0.0f;
}

bool OnsetDetector::process (float x, bool armed, float ratio, float floorFlux)
{
    if (ring.empty())
        return false;

    ring[(size_t) ringPos] = std::isfinite (x) ? x : 0.0f;
    if (++ringPos >= frame)
        ringPos = 0;

    if (++sinceHop < hop)
        return false;
    sinceHop = 0;
    return analyse (armed, ratio, floorFlux);
}

bool OnsetDetector::analyse (bool armed, float ratio, float floorFlux)
{
    for (int i = 0; i < frame; ++i)
    {
        int p = ringPos + i;
        if (p >= frame)
            p -= frame;
        re[(size_t) i] = ring[(size_t) p] * window[(size_t) i];
        im[(size_t) i] = 0.0f;
    }
    fft->forward (re.data(), im.data());

    // Spectral change: how much each band (except the lowest) has grown in loudness since last time.
    // Each band is compared with the loudest of itself and its two neighbours a moment ago, so a
    // slightly wandering pitch or the random flicker of noise does not count as something new.
    const float norm = 4.0f / (float) frame;                 // a full-scale sine reads 1.0 in its band
    const float perBand = 1024.0f / (float) frame;           // keeps the number the same size at any frame length
    const int   bands = frame / 2;
    for (int k = 0; k < bands; ++k)
    {
        const float mag = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * norm;
        curLog[(size_t) k] = std::log2 (1.0f + kLogScale * mag);
    }
    float flux = 0.0f;
    for (int k = 1; k < bands; ++k)
    {
        const float before = std::max (prevLog[(size_t) (k - 1)], std::max (prevLog[(size_t) k], prevLog[(size_t) std::min (k + 1, bands - 1)]));
        const float up = curLog[(size_t) k] - before;
        if (up > 0.0f)
            flux += up;
    }
    std::swap (prevLog, curLog);
    flux *= perBand;
    lastFlux = flux;

    ++frames;
    if (frames <= 2)                                          // the first looks only fill prevLog
    {
        lastThr = 1e9f;
        return false;
    }

    float mean = 0.0f;
    if (histCount > 0)
    {
        for (int i = 0; i < histCount; ++i)
            mean += history[(size_t) i];
        mean /= (float) histCount;
    }

    const float thr = mean * ratio + floorFlux;
    lastThr = thr;
    const bool above = flux > thr;
    if (refractory > 0)
        --refractory;
    const bool fire  = above && ! prevAbove && armed && histCount >= kWarmUp && refractory == 0;
    prevAbove = above;
    if (fire)
        refractory = std::max (1, (int) (kRefractory * sampleRate / (double) hop));

    history[(size_t) histPos] = flux;
    if (++histPos >= kHistory)
        histPos = 0;
    if (histCount < kHistory)
        ++histCount;

    return fire;
}

} // namespace ceo
