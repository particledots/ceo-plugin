// Ceo engine: a small fast Fourier transform (our own code, no library).
//
// Used by the onset detector (to see how a sound's spectrum is changing) and by
// Wavetable mode (to turn a slice of recorded audio into a band-limited single cycle).

#pragma once

#include <vector>

namespace ceo
{

class Fft
{
public:
    // size must be a power of two, 4 or more.
    explicit Fft (int size);

    int size() const { return n; }

    // In-place complex transforms on 'size' values (real parts in re, imaginary parts in im).
    // forward: X[k] = sum of x[j] * exp(-2 pi i j k / size)
    // inverse: the opposite, including the 1/size scaling, so inverse (forward (x)) gives x back.
    void forward (float* re, float* im) const { run (re, im, false); }
    void inverse (float* re, float* im) const { run (re, im, true); }

private:
    void run (float* re, float* im, bool inverse) const;

    int n;
    std::vector<int>   bitReversed;
    std::vector<float> cosTable;
    std::vector<float> sinTable;
};

} // namespace ceo
