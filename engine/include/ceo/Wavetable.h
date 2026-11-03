// Ceo engine: Wavetable mode. Our own design.
//
// A short slice of a recorded layer (2048 samples, about 43 ms at 48 kHz) is turned into
// ONE CYCLE of an oscillator: its ends are faded so the cycle joins up with itself, and
// the whole slice is squeezed into one period. Played at a pitch, partial number k of the
// slice becomes harmonic number k of the note, so the colour of the recording is heard as
// the colour of the note.
//
// To stop the highest harmonics folding back as false low notes, the cycle is rebuilt from
// its spectrum with only the harmonics that fit below the sample rate's limit for the
// pitch being played (a "band-limited" cycle).

#pragma once

#include "ceo/Fft.h"

#include <vector>

namespace ceo
{

constexpr int kWtSize = 2048;               // samples in one cycle
constexpr int kWtBins = kWtSize / 2 + 1;
constexpr float kWtMaxLift = 6.0f;

// The spectrum of one slice (bin 0, the steady offset, is always removed). It remembers how loud
// the slice was, so a cycle rebuilt with fewer harmonics can be brought back to the same loudness
// (by at most kWtMaxLift times).
struct WtSpectrum
{
    float re[kWtBins];
    float im[kWtBins];
    float rms = 0.0f;        // loudness of the faded slice it came from
};

// One cycle ready to read. Layout: d[0] is the sample before the cycle's first, d[1..kWtSize] the
// cycle, d[kWtSize+1..kWtSize+2] the first two samples again, so smooth interpolation needs no wrap checks.
struct WtTable
{
    float d[kWtSize + 3];
};

// How many harmonics (up to 1023) a note at this pitch may have without folding back.
int wtMaxHarmonic (double frequencyHz, double sampleRate);

class WtBuilder
{
public:
    WtBuilder();

    // Turns kWtSize samples into a spectrum (ends faded, offset removed).
    void analyse (const float* slice, WtSpectrum& out);

    // Rebuilds one cycle from a spectrum using harmonics 1..maxHarmonic.
    void synthesise (const WtSpectrum& spectrum, int maxHarmonic, WtTable& out);

private:
    Fft fft;
    std::vector<float> re, im, window;
};

// One oscillator: plays a table at a pitch, and can melt from its current table into a new one.
class WtVoice
{
public:
    void prepare (double sampleRate);
    void clear();                          // no table: silent

    bool hasTable() const    { return has; }
    int  harmonics() const   { return harm; }

    // Make a new table from a slice (and a second slice for the right side of a stereo layer,
    // or nullptr). It is reached gradually over morphSamples (instantly if that is under 2,
    // or if there was no table before).
    void setTarget (WtBuilder& builder, const float* sliceL, const float* sliceR, int maxHarmonic, float morphSamples);

    // Same slice, different harmonic limit (the pitch moved into another band).
    void relevel (WtBuilder& builder, int maxHarmonic, float morphSamples);

    void setFrequency (double hz, bool immediately);
    void restartPhase() { phase = 0.0; }

    // One sample. r equals l for a mono table.
    void tick (float& l, float& r);

private:
    void bake();
    void synthInto (WtBuilder& builder, int slot);

    WtSpectrum spec[2];                    // spectrum of the table being moved towards (L, R)
    bool   specStereo = false;
    WtTable tab[2][2];                     // [slot][channel]
    bool   slotStereo[2] = { false, false };
    int    cur = 0;
    bool   morphing = false;
    float  fade = 0.0f, fadeInc = 0.0f;
    int    harm = 0;
    bool   has = false;

    double sr = 48000.0;
    double phase = 0.0;
    double freq = 100.0, freqTarget = 100.0, freqCoef = 0.01;
};

} // namespace ceo
