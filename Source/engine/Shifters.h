#pragma once

// The three ways Divebomb shifts pitch.
//
// GrainShifter: a delay line read by two taps that slide at the pitch ratio, crossfading as each
// wraps. The pitch is exact. The grain length decides the sound: lined up with the note's cycles
// (Classic) it is clean on single notes and warbles on chords like the original. Average delay is
// half a grain, so a short grain is a fast one.
// BandShifter: three grain shifters on three bands, long grains low and short grains high, for
// chords with low delay (Live Chords).
// PvShifter: a phase vocoder that moves each partial and keeps the bins around it locked to it,
// cleaner on chords, with a fixed delay of one frame (Studio).

#include "Dsp.h"

namespace db
{
class GrainShifter
{
public:
    void prepare (double sampleRate, double maxGrainSeconds)
    {
        sr = sampleRate;
        maxGrain = (float) (maxGrainSeconds * sr);
        buf.assign ((size_t) nextPow2 ((int) (maxGrain * 1.2f) + 64), 0.0f);
        mask = (int) buf.size() - 1;
        grainTarget = std::min (maxGrain, (float) (0.006 * sr));
        reset();
    }
    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;
        tap[0] = { 0.5f, grainTarget, false };
        tap[1] = { 0.0f, grainTarget, false };
    }
    void setGrain (float samples) { grainTarget = std::max (32.0f, std::min (maxGrain, samples)); }

    // keep the history without reading it
    inline void feed (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    inline float process (float x, float ratio)
    {
        buf[(size_t) w] = x;
        const float slope = 1.0f - ratio;
        float y = 0.0f, gsum = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            auto& t = tap[(size_t) k];
            const auto& o = tap[(size_t) (1 - k)];
            if (std::abs (slope) < 1e-5f)
            {
                // at unison, slowly park the taps so two copies don't comb
                const float park = k == 0 ? 0.5f : 0.0f, rate = 1.0f / (float) sr;
                t.phi += (park > t.phi ? 1.0f : -1.0f) * std::min (rate, std::abs (park - t.phi));
                t.wait = false;
            }
            else if (t.wait)
            {
                // silent at its wrap point until its partner is at full level, so the pair is
                // half a grain apart again and the crossfade stays smooth
                if (std::abs (o.phi - 0.5f) <= std::abs (slope / o.len) * 1.01f + 1e-6f || o.wait) t.wait = false;
            }
            else
            {
                t.phi += slope / t.len;
                // a tap takes up a new grain length only as it wraps, where it is silent,
                // so the length can follow the note with no click and no pitch swoop
                if (t.phi >= 1.0f || t.phi < 0.0f)
                {
                    t.phi = t.phi >= 1.0f ? 0.0f : 1.0f;
                    if (std::abs (t.len - grainTarget) > 0.5f) { t.len = grainTarget; t.wait = true; }
                }
            }
            const float sn = std::sin (kPi * t.phi);
            const float g = sn * sn;
            y += g * read (kMinDelay + t.phi * t.len);
            gsum += g;
        }
        w = (w + 1) & mask;
        // with two different lengths the taps drift off half a grain apart: keep the level steady
        return gsum < 0.999f ? y / std::max (0.5f, gsum) : y;
    }

private:
    static constexpr float kMinDelay = 2.0f;
    struct Tap { float phi, len; bool wait; };

    inline float read (float d) const
    {
        const float rp = (float) w - d;
        const int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)];
        const float y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
        // 4-point Hermite
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    double sr = 48000;
    std::vector<float> buf;
    int mask = 0, w = 0;
    std::array<Tap, 2> tap {};
    float grainTarget = 288, maxGrain = 4800;
};

class BandShifter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        xo[0].set (sr, 250.0);
        xo[1].set (sr, 2000.0);
        static const double grains[3] = { 0.020, 0.008, 0.004 };
        for (int b = 0; b < 3; ++b)
        {
            band[b].prepare (sr, 0.05);
            band[b].setGrain ((float) (grains[b] * sr));
        }
        reset();
    }
    void reset() { xo[0].reset(); xo[1].reset(); for (auto& b : band) b.reset(); }
    inline void feed (float x)
    {
        float lo, rest, mid, hi;
        xo[0].split (x, lo, rest);
        xo[1].split (rest, mid, hi);
        band[0].feed (lo); band[1].feed (mid); band[2].feed (hi);
    }

    inline float process (float x, float ratio)
    {
        float lo, rest, mid, hi;
        xo[0].split (x, lo, rest);
        xo[1].split (rest, mid, hi);
        return band[0].process (lo, ratio) + band[1].process (mid, ratio) + band[2].process (hi, ratio);
    }

private:
    double sr = 48000;
    Crossover xo[2];
    GrainShifter band[3];
};

class PvShifter
{
public:
    void prepare (double sampleRate, int frameSize)
    {
        (void) sampleRate;
        n = frameSize;
        hop = n / 4;
        fft.init (n);
        win.resize ((size_t) n);
        float sumSq = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) n);
            sumSq += win[(size_t) i] * win[(size_t) i];
        }
        scale = (float) hop / sumSq / (float) n;
        in.assign ((size_t) n, 0.0f);
        acc.assign ((size_t) n * 2, 0.0f);
        spec.assign ((size_t) n, {});
        out.assign ((size_t) n, {});
        mag.assign ((size_t) n / 2 + 1, 0.0f);
        ph.assign ((size_t) n / 2 + 1, 0.0f);
        prevPh.assign ((size_t) n / 2 + 1, 0.0f);
        synthPh.assign ((size_t) n / 2 + 1, 0.0f);
        trueBin.assign ((size_t) n / 2 + 1, 0.0f);
        peakOf.assign ((size_t) n / 2 + 1, 0);
        peakShift.assign ((size_t) n / 2 + 1, 0);
        peakOk.assign ((size_t) n / 2 + 1, 0);
        peakPsi.assign ((size_t) n / 2 + 1, 0.0f);
        isPeak.assign ((size_t) n / 2 + 1, 0);
        t = 0;
    }
    void reset()
    {
        std::fill (in.begin(), in.end(), 0.0f);
        std::fill (acc.begin(), acc.end(), 0.0f);
        std::fill (prevPh.begin(), prevPh.end(), 0.0f);
        std::fill (synthPh.begin(), synthPh.end(), 0.0f);
        t = 0;
    }
    int latency() const { return n; }

    // keep the input history only: no frames are worked out while nothing listens
    inline void feed (float x)
    {
        in[(size_t) (t % n)] = x;
        acc[(size_t) ((t + n) % (n * 2))] = 0.0f;
        ++t;
    }

    inline float process (float x, float r)
    {
        ratio = r;
        in[(size_t) (t % n)] = x;
        const int m = n * 2;
        const int slot = (int) ((t + m - n) % m);
        const float y = acc[(size_t) slot];
        acc[(size_t) slot] = 0.0f;
        ++t;
        if (t % hop == 0) frame();
        return y;
    }

private:
    void frame()
    {
        // the last n samples, oldest first
        for (int i = 0; i < n; ++i)
            spec[(size_t) i] = { in[(size_t) ((t + i) % n)] * win[(size_t) i], 0.0f };
        fft.forward (spec.data());

        const int half = n / 2;
        const float expect = kTwoPi * (float) hop / (float) n;
        for (int k = 0; k <= half; ++k)
        {
            mag[(size_t) k] = std::abs (spec[(size_t) k]);
            ph[(size_t) k] = std::arg (spec[(size_t) k]);
            float d = ph[(size_t) k] - prevPh[(size_t) k] - expect * (float) k;
            d -= kTwoPi * std::round (d / kTwoPi);
            trueBin[(size_t) k] = (float) k + d / expect;
            prevPh[(size_t) k] = ph[(size_t) k];
        }

        // peaks, and which peak each bin belongs to
        float maxMag = 0.0f;
        for (int k = 0; k <= half; ++k) maxMag = std::max (maxMag, mag[(size_t) k]);
        const float floor = maxMag * 1e-4f;
        for (int k = 0; k <= half; ++k)
        {
            const float v = mag[(size_t) k];
            bool p = v > floor;
            for (int j = 1; j <= 2 && p; ++j)
            {
                if (k - j >= 0 && mag[(size_t) (k - j)] >= v) p = false;
                if (k + j <= half && mag[(size_t) (k + j)] > v) p = false;
            }
            isPeak[(size_t) k] = p ? 1 : 0;
        }
        int lastPeak = -1;
        for (int k = 0; k <= half; ++k)
        {
            if (isPeak[(size_t) k])
            {
                // bins between two peaks split at the lowest point
                if (lastPeak >= 0)
                {
                    int lo = lastPeak;
                    for (int j = lastPeak; j < k; ++j) if (mag[(size_t) j] < mag[(size_t) lo]) lo = j;
                    for (int j = lo; j < k; ++j) peakOf[(size_t) j] = k;
                }
                else for (int j = 0; j < k; ++j) peakOf[(size_t) j] = k;
                peakOf[(size_t) k] = k;
                lastPeak = k;
            }
            else peakOf[(size_t) k] = lastPeak;
        }
        if (lastPeak < 0) { std::fill (out.begin(), out.end(), std::complex<float> {}); finish(); return; }

        // each peak moves to its new frequency and takes the bins around it along, phases locked to it
        for (int p = 0; p <= half; ++p)
        {
            if (! isPeak[(size_t) p]) continue;
            const float target = trueBin[(size_t) p] * ratio;
            const int shift = (int) std::lround (target) - p;
            const int tp = p + shift;
            peakShift[(size_t) p] = shift;
            peakOk[(size_t) p] = (tp >= 0 && tp <= half) ? 1 : 0;
            if (peakOk[(size_t) p]) peakPsi[(size_t) p] = synthPh[(size_t) tp] + expect * target - ph[(size_t) p];
        }
        std::fill (out.begin(), out.end(), std::complex<float> {});
        for (int j = 0; j <= half; ++j)
        {
            const int p = peakOf[(size_t) j];
            if (p < 0 || ! peakOk[(size_t) p]) continue;
            const int k = j + peakShift[(size_t) p];
            if (k < 0 || k > half) continue;
            out[(size_t) k] += std::polar (mag[(size_t) j], peakPsi[(size_t) p] + ph[(size_t) j]);
        }
        for (int k = 0; k <= half; ++k) synthPh[(size_t) k] = std::arg (out[(size_t) k]);
        finish();
    }

    void finish()
    {
        const int half = n / 2;
        for (int k = 1; k < half; ++k) out[(size_t) (n - k)] = std::conj (out[(size_t) k]);
        out[0] = { out[0].real(), 0.0f };
        out[(size_t) half] = { out[(size_t) half].real(), 0.0f };
        fft.inverse (out.data());
        const int m = n * 2;
        const long long start = t - n;   // the frame covers the last n samples
        for (int i = 0; i < n; ++i)
            acc[(size_t) ((start + i + m) % m)] += out[(size_t) i].real() * win[(size_t) i] * scale;
    }

    int n = 1024, hop = 256;
    long long t = 0;
    float ratio = 1.0f, scale = 1.0f;
    Fft fft;
    std::vector<float> win, in, acc, mag, ph, prevPh, synthPh, trueBin, peakPsi;
    std::vector<int> peakOf, peakShift;
    std::vector<char> isPeak, peakOk;
    std::vector<std::complex<float>> spec, out;
};

// One shifted voice. Near unison it hands over to the plain input, so heel down is truly dry
// and costs no delay (in Studio the plain input is delayed to line up with the vocoder).
class Voice
{
public:
    enum Mode { Classic = 0, LiveChords = 1, Studio = 2 };

    void prepare (double sampleRate, int studioFrame)
    {
        sr = sampleRate;
        classic.prepare (sr, 0.09);
        bands.prepare (sr);
        pvs[0].prepare (sr, studioFrame);
        pvs[1].prepare (sr, studioFrame * 2);
        dryDelay.assign ((size_t) nextPow2 (studioFrame * 2 + 8), 0.0f);
        dmask = (int) dryDelay.size() - 1;
        reset();
    }
    void reset()
    {
        classic.reset(); bands.reset(); pvs[0].reset(); pvs[1].reset();
        std::fill (dryDelay.begin(), dryDelay.end(), 0.0f);
        dw = 0; shiftMix = 0.0f;
    }
    void setMode (Mode m, bool largeFrame)
    {
        if (m != mode || largeFrame != large) { mode = m; large = largeFrame; reset(); }
    }
    void setClassicGrain (float samples) { classic.setGrain (samples); }
    bool idle() const { return shiftMix <= 0.0f; }

    inline float process (float x, float semis, bool allowDry)
    {
        auto& pv = pvs[large ? 1 : 0];
        const bool shifting = ! allowDry || std::abs (semis) > 0.01f;
        const float fade = 1.0f / (float) (0.008 * sr);
        shiftMix = shifting ? std::min (1.0f, shiftMix + fade) : std::max (0.0f, shiftMix - fade);

        float direct = x;
        if (mode == Studio)
        {
            dryDelay[(size_t) dw] = x;
            direct = dryDelay[(size_t) ((dw - pv.latency()) & dmask)];
            dw = (dw + 1) & dmask;
        }
        if (shiftMix <= 0.0f)
        {
            // keep the history running so it starts cleanly
            if (mode == Studio) pv.feed (x);
            else if (mode == LiveChords) bands.feed (x);
            else classic.feed (x);
            return direct;
        }
        const float ratio = semisToRatio (semis);
        float y;
        if (mode == Studio) y = pv.process (x, ratio);
        else if (mode == LiveChords) y = bands.process (x, ratio);
        else y = classic.process (x, ratio);
        return direct + (y - direct) * shiftMix;
    }

private:
    double sr = 48000;
    Mode mode = Classic;
    GrainShifter classic;
    BandShifter bands;
    PvShifter pvs[2];
    bool large = false;
    std::vector<float> dryDelay;
    int dmask = 0, dw = 0;
    float shiftMix = 0.0f;
};
} // namespace db
