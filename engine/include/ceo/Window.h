// Ceo engine: the amplitude envelope ("window") of a single grain.
//
// Grain window knob: centre = smooth bell (Gaussian). Turned down it morphs
// towards a square (flat, with short soft edges). Turned up it morphs towards a
// descending ramp (sharp start, then fades out), which sounds percussive.

#pragma once

namespace ceo
{

class GrainWindow
{
public:
    // knob: 0..1 (0.5 = bell). lengthSeconds: grain length, used to keep the
    // soft edges of the square and ramp shapes a sensible fixed time (click-free).
    void set (float knob, float lengthSeconds);

    // Envelope value at position t (0 = grain start, 1 = grain end). Result 0..1.
    float at (float t) const;

    // A gain that evens out loudness between window shapes (a square window has
    // far more energy than a narrow bell), so changing the shape does not jump the level.
    float loudnessCompensation() const { return comp; }

private:
    float morph   = 0.0f;
    bool  ramp    = false;
    float fadeSq  = 0.02f;   // each edge of the square, as a fraction of the grain
    float fadeRmp = 0.02f;   // attack of the ramp, as a fraction of the grain
    float comp    = 1.0f;
};

} // namespace ceo
