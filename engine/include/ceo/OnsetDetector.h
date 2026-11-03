// Ceo engine: onset ("attack") detection. Our own design.
//
// Every few milliseconds the detector looks at the spectrum of the last ~20 ms of
// sound and adds up how much louder each frequency band has become since the previous
// look (on a logarithmic loudness scale, so quiet and loud sounds are treated alike
// apart from a noise floor). That number is the "spectral change". A new note, a drum
// hit or a pluck makes it jump. An onset is reported when the change rises above
//     (average change over the last third of a second) * ratio + floor
// having been below it before, so a long loud sound reports one onset, not many. After an
// onset nothing more is reported for 60 ms.

#pragma once

#include "ceo/Fft.h"

#include <memory>
#include <vector>

namespace ceo
{

class OnsetDetector
{
public:
    // Allocates memory: never call from the audio thread.
    void prepare (double sampleRate);

    // Forget the recent past (call when detection is switched on).
    void reset();

    // Feed one sample of the (mono) analysis signal. Returns true on the sample where
    // a finished analysis frame found an onset. 'armed' false = never report (the
    // detector keeps listening so it does not fire the moment it is armed again).
    bool process (float x, bool armed, float ratio, float floorFlux);

    // For the self-tests.
    float lastChange() const    { return lastFlux; }
    float lastThreshold() const { return lastThr; }
    int   frameSize() const     { return frame; }
    int   hopSize() const       { return hop; }

private:
    bool analyse (bool armed, float ratio, float floorFlux);

    int frame = 1024;
    int hop   = 256;
    std::unique_ptr<Fft> fft;

    std::vector<float> ring;        // last 'frame' samples
    std::vector<float> window;      // Hann
    std::vector<float> re, im;
    std::vector<float> prevLog;     // log loudness of each band in the previous frame
    std::vector<float> curLog;      // ... and in the frame being looked at now
    std::vector<float> history;     // recent spectral-change values
    int   ringPos   = 0;
    int   sinceHop  = 0;
    int   frames    = 0;            // frames analysed since reset
    int   histCount = 0;
    int   histPos   = 0;
    int   refractory = 0;           // frames left in which no further onset is reported
    double sampleRate = 48000.0;
    bool  prevAbove = false;
    float lastFlux  = 0.0f;
    float lastThr   = 0.0f;
};

} // namespace ceo
