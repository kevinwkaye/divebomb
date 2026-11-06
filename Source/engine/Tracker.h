#pragma once

// Pitch tracker for the Classic shifter. It only sets the grain length so grains line up with the
// note's cycles: the pitch shift itself is exact whatever the tracker says. That is how a bend stays
// in tune while the tracker's occasional wrong guesses give the original's warble.
//
// YIN on a copy of the input decimated by 8, looking for fundamentals from 28 Hz (below a low B
// at 30.9 Hz) to 1 kHz. Silence is judged against how loud you have been playing, never a fixed
// level, so a fading low note keeps its pitch.

#include "Dsp.h"

namespace db
{
class Tracker
{
public:
    static constexpr int kDecimate = 8;
    static constexpr float kLowest = 28.0f, kHighest = 1000.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        dsr = sr / kDecimate;
        aa[0].lowPass (sr, 1100.0, 0.54);
        aa[1].lowPass (sr, 1100.0, 1.31);
        tauMax = (int) std::ceil (dsr / kLowest);
        tauMin = std::max (2, (int) std::floor (dsr / kHighest));
        window = tauMax * 2;
        ring.assign ((size_t) nextPow2 (window * 2 + 8), 0.0f);
        mask = (int) ring.size() - 1;
        diff.assign ((size_t) tauMax + 2, 0.0f);
        hop = std::max (1, (int) (sr * 0.0053 / kDecimate));
        playEnv.setTime (sr, 4.0);
        fastEnv.setTime (sr, 0.01);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        aa[0].reset(); aa[1].reset();
        pos = 0; phase = 0; sinceHop = 0;
        period = 0; confidence = 0; lastGood = 0; heldFor = 0;
        hist[0] = hist[1] = hist[2] = 0;
        playEnv.y = fastEnv.y = 0;
    }

    // glitch 0 tracks cleanly, 1 is the original's character, 2 more so
    void setGlitch (float g) { glitch = g; }

    inline void push (float x)
    {
        const float a = std::abs (x);
        fastEnv.process (a);
        // the playing level follows loud playing quickly and lets go slowly
        if (fastEnv.y > playEnv.y) playEnv.y += (fastEnv.y - playEnv.y) * 0.002f;
        else playEnv.process (fastEnv.y);

        const float y = aa[1].process (aa[0].process (x));
        if (++phase >= kDecimate)
        {
            phase = 0;
            ring[(size_t) pos] = y;
            pos = (pos + 1) & mask;
            if (++sinceHop >= hop) { sinceHop = 0; estimate(); }
        }
    }

    // the tracked period in samples at the full rate, 0 if nothing is tracked
    float periodSamples() const { return period; }
    float frequency() const { return period > 0 ? (float) sr / period : 0.0f; }
    float rngJitter = 0.0f;

private:
    void estimate()
    {
        // quiet relative to your playing, or near digital silence: hold the last pitch a little while
        const bool quiet = fastEnv.y < 0.00002f || fastEnv.y < playEnv.y * 0.0708f; // 23 dB under the playing level
        if (quiet) return;   // hold the last pitch: the next note is often in the same place

        // YIN difference over the last window
        const int start = (pos - window + mask + 1) & mask;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < window - tauMax; ++j)
            {
                const float e = ring[(size_t) ((start + j) & mask)] - ring[(size_t) ((start + j + tau) & mask)];
                d += e * e;
            }
            diff[(size_t) tau] = d;
        }
        // cumulative mean normalised difference
        float running = 0.0f;
        diff[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = running > 0.0f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
        }
        // the cleaner the setting, the stricter the threshold
        const float threshold = 0.10f + 0.10f * std::min (2.0f, glitch);
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (diff[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            // nothing clear: the original grabs whatever looks closest, a cleaner setting holds on
            if (glitch >= 0.5f)
            {
                float m = 1e9f;
                for (int tau = tauMin; tau < tauMax; ++tau) if (diff[(size_t) tau] < m) { m = diff[(size_t) tau]; best = tau; }
                if (m > 0.6f) best = -1;
            }
            if (best < 0) return;
        }
        // parabolic interpolation
        float t = (float) best;
        if (best > 1 && best < tauMax - 1)
        {
            const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1e-9f) t += 0.5f * (a - c) / den;
        }
        confidence = 1.0f - diff[(size_t) best];
        float p = t * kDecimate;

        // a clean setting takes the median of three estimates, which rides out single mistakes
        hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = p;
        if (glitch < 0.5f && hist[2] > 0.0f)
        {
            float a = hist[0], b = hist[1], c = hist[2];
            p = std::max (std::min (a, b), std::min (std::max (a, b), c));
        }
        if (glitch > 1.0f) p *= 1.0f + (glitch - 1.0f) * 0.2f * rngJitter;
        period = p;
        lastGood = p;
        heldFor = 0;
    }

    double sr = 48000, dsr = 6000;
    Biquad aa[2];
    std::vector<float> ring, diff;
    int mask = 0, pos = 0, phase = 0, sinceHop = 0, hop = 32;
    int tauMin = 6, tauMax = 214, window = 428;
    float period = 0, confidence = 0, lastGood = 0, glitch = 1.0f;
    double heldFor = 0;
    float hist[3] {};
    Smooth playEnv, fastEnv;
};
} // namespace db
