// Offline engine test, no JUCE. Renders test notes through the engine and fails the build if:
//  - a shifted bass note (down to low B, 30.9 Hz) is more than 5 cents out, in any model
//  - a fading low note loses its pitch
//  - any setting's wet level drops more than 20 dB below the input (plucked, quiet, pad, late start)
//  - footswitch combos or presses misbehave, or the treadle steps
// It also measures how long a shifted note takes to arrive in each mode, and CPU per instance,
// and writes both to test-results.md.

#include "DivebombEngine.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace db;

static const double SR = 48000.0;
static int failures = 0;
static std::ostringstream report;

static void fail (const std::string& what)
{
    ++failures;
    std::printf ("FAIL: %s\n", what.c_str());
}

// a plucked note: harmonics falling away, a fast attack, a slow decay
static std::vector<float> pluck (double f, double seconds, double start, float level, double decay = 1.6, bool pad = false)
{
    std::vector<float> x ((size_t) (seconds * SR), 0.0f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / SR - start;
        if (t < 0) continue;
        double env = pad ? std::min (1.0, t / 0.8) : std::min (1.0, t / 0.002) * std::exp (-t / decay);
        double v = 0;
        for (int h = 1; h <= 8; ++h)
            if (f * h < SR * 0.45) v += std::sin (2.0 * 3.14159265358979 * f * h * t + h) / h;
        x[i] = (float) (level * env * v * 0.5);
    }
    return x;
}

static std::vector<float> render (Engine& e, const EngineParams& p, const std::vector<float>& in, int block = 128)
{
    std::vector<float> out (in.size(), 0.0f);
    for (size_t s = 0; s < in.size(); s += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - s);
        e.process (in.data() + s, out.data() + s, n, p);
    }
    return out;
}

static double rms (const std::vector<float>& x, double from, double to)
{
    const size_t a = (size_t) (from * SR), b = std::min (x.size(), (size_t) (to * SR));
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}

// the period of a signal near an expected frequency, by normalised autocorrelation
static double measureFreq (const std::vector<float>& x, double from, double len, double expect)
{
    const size_t a = (size_t) (from * SR), n = (size_t) (len * SR);
    if (a + n * 2 >= x.size()) return 0;
    const int lo = (int) (SR / (expect * 1.12)), hi = (int) (SR / (expect * 0.89)) + 1;
    double best = -1e9; int bt = lo;
    std::vector<double> c ((size_t) hi + 2, 0.0);
    for (int tau = lo - 1; tau <= hi + 1; ++tau)
    {
        double num = 0, d1 = 0, d2 = 0;
        for (size_t i = 0; i < n; ++i) { num += (double) x[a + i] * x[a + i + (size_t) tau]; d1 += (double) x[a + i] * x[a + i]; d2 += (double) x[a + i + (size_t) tau] * x[a + i + (size_t) tau]; }
        c[(size_t) tau] = num / std::sqrt (d1 * d2 + 1e-20);
        if (tau >= lo && tau <= hi && c[(size_t) tau] > best) { best = c[(size_t) tau]; bt = tau; }
    }
    double t = bt;
    const double ya = c[(size_t) bt - 1], yb = c[(size_t) bt], yc = c[(size_t) bt + 1], den = ya - 2 * yb + yc;
    if (std::abs (den) > 1e-12) t += 0.5 * (ya - yc) / den;
    return SR / t;
}

static double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

// when a signal first reaches 30% of its level 100 to 300 ms after it starts
static double onset (const std::vector<float>& x, double searchFrom)
{
    std::vector<float> env (x.size(), 0.0f);
    float e = 0;
    const float a = std::exp (-1.0f / (float) (0.001 * SR));
    for (size_t i = 0; i < x.size(); ++i) { e = std::abs (x[i]) + a * (e - std::abs (x[i])); env[i] = e; }
    size_t first = (size_t) (searchFrom * SR);
    while (first < env.size() && env[first] < 1e-4f) ++first;
    if (first >= env.size()) return -1;
    float ref = 0;
    for (size_t i = first + (size_t) (0.1 * SR); i < std::min (env.size(), first + (size_t) (0.3 * SR)); ++i) ref = std::max (ref, env[i]);
    for (size_t i = (size_t) (searchFrom * SR); i < env.size(); ++i)
        if (env[i] >= 0.3f * ref) return (double) i / SR;
    return -1;
}

static const char* noteName (double f)
{
    if (f < 32) return "Low B (31 Hz)";
    if (f < 45) return "Low E (41 Hz)";
    if (f < 60) return "A (55 Hz)";
    if (f < 90) return "Guitar low E (82 Hz)";
    if (f < 120) return "A (110 Hz)";
    if (f < 400) return "E (330 Hz)";
    return "A (440 Hz)";
}

int main()
{
    // ------------------------------------------------------------------ bass tracking, every model
    report << "# Divebomb render test\n\n## Bass tracking\n\nBend 1 OCT UP at toe in Live Classic. Pitch of the shifted note against a true octave.\n\n"
           << "| Model | Low B | Low E | A 55 | E 82 | A 110 |\n| --- | --- | --- | --- | --- | --- |\n";
    const double notes[5] = { 30.87, 41.20, 55.0, 82.41, 110.0 };
    for (int m = 0; m < kModels; ++m)
    {
        report << "| " << modelName (m);
        for (double f : notes)
        {
            Engine e; e.prepare (SR, 128);
            EngineParams p; p.model = m; p.setting = 1; p.treadle = 1.0f; p.anchor = 2;
            const auto in = pluck (f, 2.0, 0.1, 0.5f);
            const auto out = render (e, p, in);
            const double got = measureFreq (out, 0.7, 0.5, f * 2.0);
            const double c = cents (got, f * 2.0);
            char buf[64]; std::snprintf (buf, sizeof buf, " | %+.1f c", c);
            report << buf;
            if (std::abs (c) > 5.0) fail (std::string (modelName (m)) + " octave up on " + noteName (f) + " is " + std::to_string (c) + " cents out");
        }
        report << " |\n";
    }

    // a low B fading away keeps its pitch: 1.6 s later it is about 24 dB down
    {
        Engine e; e.prepare (SR, 128);
        EngineParams p; p.model = ModelBass; p.setting = 1; p.treadle = 1.0f; p.anchor = 2;
        const auto in = pluck (30.87, 3.0, 0.1, 0.5f, 0.6);
        const auto out = render (e, p, in);
        const double got = measureFreq (out, 1.6, 0.5, 61.74);
        const double c = cents (got, 61.74);
        char buf[160]; std::snprintf (buf, sizeof buf, "\nA fading low B, 1.5 s into its decay (about 24 dB down): %+.1f cents.\n", c);
        report << buf;
        if (std::abs (c) > 5.0) fail ("fading low B drifts " + std::to_string (c) + " cents");
    }

    // octave down and the drop on bass
    {
        Engine e; e.prepare (SR, 128);
        EngineParams p; p.model = ModelBass; p.setting = 8; p.treadle = 1.0f; p.anchor = 2;
        const auto in = pluck (82.41, 2.0, 0.1, 0.5f);
        const double c = cents (measureFreq (render (e, p, in), 0.8, 0.3, 41.2), 41.2);
        if (std::abs (c) > 5.0) fail ("bass octave down " + std::to_string (c) + " cents");
        char buf[120]; std::snprintf (buf, sizeof buf, "Bass 1 OCT DN from 82 Hz: %+.1f cents.\n", c);
        report << buf;
    }

    // ------------------------------------------------------------------ wet level (DSP lessons)
    int levelChecks = 0;
    for (int m = 0; m < kModels; ++m)
    {
        const auto& tab = tableFor (m);
        for (int s = 0; s < tab.count; ++s)
            for (int kind = 0; kind < 3; ++kind)
                for (int algo = 0; algo < 3; ++algo)
                {
                    Engine e; e.prepare (SR, 128);
                    EngineParams p; p.model = m; p.setting = s; p.treadle = 1.0f;
                    p.algo = algo == 1 ? 1 : 0; p.latency = algo == 2 ? 1 : 0;
                    const double start = kind == 2 ? 2.0 : 0.1;
                    const auto in = kind == 1 ? pluck (196.0, 3.0, start, 0.5f, 8.0, true)
                                              : pluck (110.0, 3.4, start, kind == 0 ? 0.5f : 0.0158f);
                    const auto out = render (e, p, in);
                    const double a = start + 0.3, b = start + 1.0;
                    const double ri = rms (in, a, b), ro = rms (out, a + 0.05, b + 0.05);
                    ++levelChecks;
                    if (ro < ri * 0.1)
                    {
                        static const char* kinds[3] = { "plucked", "pad", "late start" };
                        static const char* algos[3] = { "Classic", "Live Chords", "Studio" };
                        fail (std::string (modelName (m)) + " " + tab.s[(size_t) s].name + " " + kinds[kind] + " " + algos[algo]
                              + " wet level " + std::to_string (20 * std::log10 (ro / ri + 1e-9)) + " dB");
                    }
                }
    }
    report << "\n## Level\n\n" << levelChecks << " renders (every setting of every model, plucked, pad and late start, in all three modes): wet level never more than 20 dB under the input unless listed as a failure.\n";

    // ------------------------------------------------------------------ footswitches
    {
        // stomp section, B hammer: the jump starts in the chunk the press lands in
        Engine e; e.prepare (SR, 64);
        EngineParams p; p.section = 1; p.p1 = 12;
        std::vector<float> in (64, 0.1f), out (64);
        e.footDown (1, 10);
        e.process (in.data(), out.data(), 64, p);
        if (! e.uiP1.load()) fail ("footswitch B hammer didn't land in its own buffer");
        e.footUp (1, 5);
        e.process (in.data(), out.data(), 64, p);
        if (e.uiP1.load()) fail ("footswitch B hammer didn't let go");

        // C then D within 50 ms is a combo: neither pitch job stays on
        p.hamMode = HamHammer;
        e.footDown (2, 0);
        e.process (in.data(), out.data(), 64, p);
        for (int k = 0; k < 37; ++k) e.process (in.data(), out.data(), 64, p);   // ~50 ms
        e.footDown (3, 0);
        e.process (in.data(), out.data(), 64, p);
        if (e.uiP2.load() || e.uiDropActive.load()) fail ("C+D combo left a pitch job on");
        e.footUp (2, 0); e.footUp (3, 0);
        e.process (in.data(), out.data(), 64, p);
        if (e.uiDropActive.load()) fail ("C+D combo latched the drop");

        // the drop, momentary: on while held
        e.footDown (3, 0); e.process (in.data(), out.data(), 64, p);
        if (! e.uiDropActive.load()) fail ("drop didn't engage");
        e.footUp (3, 0); e.process (in.data(), out.data(), 64, p);
        if (e.uiDropActive.load()) fail ("drop didn't let go");

        // treadle section D: a tap latches, a hold is momentary
        EngineParams t; t.section = 0;
        e.footDown (3, 0); e.process (in.data(), out.data(), 64, t);
        if (! e.uiLayerActive.load()) fail ("shift layer didn't engage on press");
        e.footUp (3, 0); e.process (in.data(), out.data(), 64, t);
        if (! e.latched (0)) fail ("a tap on D didn't latch the shift layer");
        e.footDown (3, 0); for (int k = 0; k < 400; ++k) e.process (in.data(), out.data(), 64, t);
        if (e.uiLayerActive.load()) fail ("holding D didn't flip the latched layer off");
        e.footUp (3, 0); e.process (in.data(), out.data(), 64, t);
        if (! e.latched (0) || ! e.uiLayerActive.load()) fail ("a hold on D changed the latch");
        report << "\n## Footswitches\n\nHammer presses land in the buffer they're pressed in. Combos take both presses back. A tap on D latches the shift layer and a hold is momentary.\n";
    }

    // the treadle: MIDI arriving every 10 ms glides with no steps and no lag
    {
        Engine e; e.prepare (SR, 64);
        EngineParams p; p.section = 0;
        std::vector<float> in (64, 0.0f), out (64);
        float maxStep = 0, last = 0, lag = 0;
        int cc = 0;
        for (int b = 0; b < 2000; ++b)
        {
            if (b % 7 == 0 && cc < 127) { ++cc; e.treadleCc ((float) cc / 127.0f, 0); }
            e.process (in.data(), out.data(), 64, p);
            const float v = e.uiTreadle.load();
            maxStep = std::max (maxStep, std::abs (v - last));
            last = v;
            if (cc == 127 && b % 7 == 6) lag = std::abs (1.0f - v);
        }
        char buf[200]; std::snprintf (buf, sizeof buf, "\nTreadle swept heel to toe by MIDI every 9 ms: largest step %.4f of the travel per 64-sample buffer, %.3f from toe once the last value arrives.\n", maxStep, lag);
        report << buf;
        if (maxStep > 0.02f) fail ("treadle steps");
    }

    // ------------------------------------------------------------------ latency
    report << "\n## Latency\n\nHow long after you pluck the shifted note arrives (Bend 1 OCT UP at toe, Low Anchor off), measured on the note's attack. "
              "Ableton adds its own buffer on top: 1.3 ms at 64 samples, 2.7 ms at 128, each way. The dry signal and the Low Anchor band arrive with no delay.\n\n"
           << "| Note | Live Classic | Live Chords | Studio | Studio, Bass model |\n| --- | --- | --- | --- | --- |\n";
    const double lat[6] = { 30.87, 41.20, 82.41, 110.0, 329.6, 440.0 };
    for (double f : lat)
    {
        report << "| " << noteName (f);
        for (int mode = 0; mode < 4; ++mode)
        {
            Engine e; e.prepare (SR, 64);
            EngineParams p; p.model = mode == 3 ? ModelBass : ModelFive; p.setting = 1; p.treadle = 1.0f; p.anchor = 2;
            p.algo = mode == 1 ? 1 : 0; p.latency = mode >= 2 ? 1 : 0;
            // warm up the tracker on the same note, then a fresh attack
            auto in = pluck (f, 3.0, 0.1, 0.5f, 0.15);
            auto second = pluck (f, 3.0, 1.5, 0.5f);
            for (size_t i = 0; i < in.size(); ++i) in[i] = (i < (size_t) (1.45 * SR) ? in[i] : 0.0f) + second[i];
            const auto out = render (e, p, in, 64);
            const double ti = onset (in, 1.45), to = onset (out, 1.45);
            char buf[48]; std::snprintf (buf, sizeof buf, " | %.1f ms", (to - ti) * 1000.0);
            report << buf;
        }
        report << " |\n";
    }

    // ------------------------------------------------------------------ CPU
    report << "\n## CPU\n\nOne instance at 48 kHz, the heaviest setting (NEON octave morph with the shift layer on, four voices), as a share of one core on the machine that ran this test.\n\n"
           << "| Buffer | Live Classic | Live Chords | Studio |\n| --- | --- | --- | --- |\n";
    for (int block : { 64, 128 })
    {
        report << "| " << block << " samples";
        for (int mode = 0; mode < 3; ++mode)
        {
            Engine e; e.prepare (SR, block);
            EngineParams p; p.model = ModelNeon; p.setting = 19; p.treadle = 0.5f; p.layerOn = true;
            p.algo = mode == 1 ? 1 : 0; p.latency = mode == 2 ? 1 : 0;
            e.setLatched (0, true);
            const auto in = pluck (110.0, 10.0, 0.0, 0.5f, 4.0);
            std::vector<float> out (in.size());
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t s = 0; s < in.size(); s += (size_t) block)
                e.process (in.data() + s, out.data() + s, (int) std::min<size_t> ((size_t) block, in.size() - s), p);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            char buf[48]; std::snprintf (buf, sizeof buf, " | %.1f%%", secs / 10.0 * 100.0);
            report << buf;
        }
        report << " |\n";
    }

    report << "\n" << (failures == 0 ? "All checks passed." : std::to_string (failures) + " checks failed.") << "\n";
    std::printf ("%s", report.str().c_str());
    std::ofstream ("test-results.md") << report.str();
    return failures == 0 ? 0 : 1;
}
