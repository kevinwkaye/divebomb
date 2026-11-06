#pragma once

// Small DSP building blocks shared by the Divebomb engine.
// Plain C++17, no JUCE and no platform code, so the engine builds for Windows x64 and Linux ARM64.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace db
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float semisToRatio (float s) { return std::exp2 (s / 12.0f); }
inline float dbToGain (float d) { return std::pow (10.0f, d / 20.0f); }
inline float clamp01 (float v) { return std::min (1.0f, std::max (0.0f, v)); }

inline int nextPow2 (int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

// RBJ biquad, transposed direct form II
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void lowPass (double sr, double f, double q = 0.70710678)
    {
        const double w = 2.0 * 3.14159265358979 * std::min (f, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
    void highPass (double sr, double f, double q = 0.70710678)
    {
        const double w = 2.0 * 3.14159265358979 * std::min (f, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (float) ((1.0 + c) * 0.5 / a0); b1 = (float) (-(1.0 + c) / a0); b2 = b0;
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
    void reset() { z1 = z2 = 0.0f; }
    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Linkwitz-Riley 4th-order crossover: the low and high outputs sum back flat
struct Crossover
{
    Biquad lp[2], hp[2];
    double sr = 48000, freq = -1;

    void set (double sampleRate, double f)
    {
        if (sampleRate == sr && std::abs (f - freq) < 0.5) return;
        sr = sampleRate; freq = f;
        for (auto& b : lp) b.lowPass (sr, f);
        for (auto& b : hp) b.highPass (sr, f);
    }
    void reset() { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }
    inline void split (float x, float& lo, float& hi)
    {
        lo = lp[1].process (lp[0].process (x));
        hi = hp[1].process (hp[0].process (x));
    }
    inline float low (float x) { return lp[1].process (lp[0].process (x)); }
    inline float high (float x) { return hp[1].process (hp[0].process (x)); }
};

// A value that glides to its target over a set time
struct Glide
{
    float value = 0, target = 0, step = 0;
    int left = 0;

    void snap (float v) { value = target = v; left = 0; }
    void to (float v, int samples)
    {
        target = v;
        if (samples <= 1) { value = v; left = 0; return; }
        step = (v - value) / (float) samples;
        left = samples;
    }
    inline float next()
    {
        if (left > 0) { value += step; if (--left == 0) value = target; }
        return value;
    }
    void advance (int n)
    {
        if (left <= 0) return;
        if (n >= left) { value = target; left = 0; }
        else { value += step * (float) n; left -= n; }
    }
};

// One-pole smoother with a time constant in seconds
struct Smooth
{
    float y = 0, a = 0;
    void setTime (double sr, double seconds) { a = seconds <= 0.0 ? 0.0f : (float) std::exp (-1.0 / (sr * seconds)); }
    inline float process (float x) { y = x + a * (y - x); return y; }
};

// Iterative radix-2 complex FFT
class Fft
{
public:
    void init (int size)
    {
        if (size == n) return;
        n = size;
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, -kTwoPi * (float) i / (float) n);
        rev.resize ((size_t) n);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }
    int size() const { return n; }

    void forward (std::complex<float>* x) const { run (x, false); }
    void inverse (std::complex<float>* x) const { run (x, true); }

private:
    void run (std::complex<float>* x, bool inv) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, stride = n / len;
            for (int s = 0; s < n; s += len)
                for (int k = 0; k < half; ++k)
                {
                    auto w = tw[(size_t) (k * stride)];
                    if (inv) w = std::conj (w);
                    const auto t = w * x[s + k + half];
                    x[s + k + half] = x[s + k] - t;
                    x[s + k] += t;
                }
        }
    }
    int n = 0;
    std::vector<std::complex<float>> tw;
    std::vector<int> rev;
};

// Tiny fast random numbers, safe on the audio thread
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    inline float next() // -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s & 0xffffff) / 8388608.0f - 1.0f;
    }
};
} // namespace db
