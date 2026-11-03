#include "ceo/Fft.h"

#include <cmath>
#include <utility>

namespace ceo
{

Fft::Fft (int size) : n (size)
{
    int bits = 0;
    while ((1 << bits) < n)
        ++bits;

    bitReversed.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if ((i >> b) & 1)
                r |= 1 << (bits - 1 - b);
        bitReversed[(size_t) i] = r;
    }

    cosTable.resize ((size_t) n / 2);
    sinTable.resize ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
    {
        const double angle = 2.0 * 3.14159265358979323846 * (double) k / (double) n;
        cosTable[(size_t) k] = (float) std::cos (angle);
        sinTable[(size_t) k] = (float) std::sin (angle);
    }
}

void Fft::run (float* re, float* im, bool inverse) const
{
    for (int i = 0; i < n; ++i)
    {
        const int j = bitReversed[(size_t) i];
        if (j > i)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        const int step = n / len;
        for (int i = 0; i < n; i += len)
        {
            for (int j = 0; j < half; ++j)
            {
                const float wr = cosTable[(size_t) (j * step)];
                const float wi = inverse ? sinTable[(size_t) (j * step)] : -sinTable[(size_t) (j * step)];
                const int a = i + j;
                const int b = a + half;
                const float tr = re[b] * wr - im[b] * wi;
                const float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }

    if (inverse)
    {
        const float scale = 1.0f / (float) n;
        for (int i = 0; i < n; ++i)
        {
            re[i] *= scale;
            im[i] *= scale;
        }
    }
}

} // namespace ceo
