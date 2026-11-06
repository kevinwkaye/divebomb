#pragma once

// Divebomb's engine. Everything that changes pitch runs here on the audio thread: the treadle,
// the momentary whammy, the shift layer, the drop, hammer-ons, trills, sequences and glides.
// Footswitch presses arrive with their sample position, so a pitch jump lands within the buffer
// it was pressed in. Every active shift for a voice is added into one number, so a drop under a
// bend is one shift with one delay.

#include "Models.h"
#include "Shifters.h"
#include "Tracker.h"
#include <atomic>

namespace db
{
struct EngineParams
{
    int section = 0;            // 0 treadle, 1 stomp
    int model = ModelFive;
    int setting = 1;
    int algo = 0;               // 0 Classic, 1 Chords
    int latency = 0;            // 0 Live, 1 Studio
    float treadle = 0.0f;       // the panel's treadle (and automation)
    float rise = 0.12f, fall = 0.25f;   // footswitch B's momentary whammy

    bool layerOn = false;       // shift layer latched (DUAL's drop tune, available in every model)
    int layerType = 0;          // 0 drop tune, 1 detune
    int layerStep = 10;         // index into layerTable(), default -2
    float layerGlide = 0.0f;    // seconds, 0 = instant like the original

    int dropStep = 1;           // index into dropTable(), default -2
    bool dropLatch = false;     // false momentary, true latching
    bool dropOn = false;        // latched state

    int jump = 0;               // 0 hammer, 1 glide on footswitch B
    int p1 = 12, p2 = 7, p2b = -5;
    int hamMode = HamHammer;
    std::array<int, 5> seq2 { 0, -2, 0, 0, 0 }, seq3 { 0, -2, -5, 0, 0 }, seq5 { 0, 3, 5, 7, 12 };
    int retrigger = 0;          // 0 play once, 1 retrigger, 2 loop
    bool dryPlus = false;
    bool trill = false;
    bool trillSync = true;
    int trillDiv = 2;           // 1/8, 1/8T, 1/16, 1/16T, 1/32
    float trillHz = 8.0f;

    int glideStep = 4;          // index into glideTable(), default octave
    bool glideDown = false;
    float glideShift = 0.04f, glideReturn = 0.18f;
    bool glideLatch = false;
    bool glideOn = false;

    int anchor = 0;             // 0 auto (bass input or the neon model), 1 on, 2 off
    float anchorHz = 100.0f;
    int input = 0;              // 0 guitar, 1 bass
    float glitch = 1.0f;
    bool origBandwidth = true;  // Chords in the stomp section rolls off above 11 kHz, as the compact pedals do
    bool trails = true;
    bool bypass = false;
    float levelDb = 0.0f;
    double bpm = 120.0;
};

class Engine
{
public:
    static constexpr int kChunk = 16, kVoices = 4, kMaxEvents = 128;
    static constexpr double kHold = 0.45, kCombo = 0.12;

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        tracker.prepare (sr);
        const int frame = sr > 70000.0 ? 2048 : 1024;
        studioFrame = frame;
        for (auto& v : voices) v.prepare (sr, frame);
        dry.assign ((size_t) nextPow2 (frame * 2 + 8), 0.0f);
        dmask = (int) dry.size() - 1;
        for (auto& b : bw) b.lowPass (sr, 11000.0);
        reset();
    }

    void reset()
    {
        tracker.reset();
        for (auto& v : voices) v.reset();
        std::fill (dry.begin(), dry.end(), 0.0f);
        dw = 0;
        anchorIn.reset(); anchorOut.reset();
        for (auto& b : bw) b.reset();
        treadleG.snap (0); mom.snap (0); layerG.snap (0); glideG.snap (0); wet.snap (1);
        for (auto& g : voiceGain) g.snap (0);
        voiceGain[0].snap (1);
        now = 0; lastTreadleEvent = -100000; nEvents = 0;
        down.fill (false); cancelled.fill (false); downAt.fill (-1000000);
    }

    // ---------------- audio thread: called before process() for this block ----------------
    void footDown (int sw, int offset) { addEvent ({ EvDown, sw, 0.0f, offset }); }
    void footUp (int sw, int offset) { addEvent ({ EvUp, sw, 0.0f, offset }); }
    void treadleCc (float v01, int offset) { addEvent ({ EvTreadle, 0, v01, offset }); }

    // the panel or a preset changed a latched state: the engine follows it
    void setLatched (int which, bool on)
    {
        if (which == 0) layerLatched = on;
        else if (which == 1) dropLatched = on;
        else glideLatched = on;
    }
    bool latched (int which) const { return which == 0 ? layerLatched : (which == 1 ? dropLatched : glideLatched); }

    int latencySamples (const EngineParams& p) const
    {
        return p.latency == 1 ? (p.model == ModelBass ? studioFrame * 2 : studioFrame) : 0;
    }

    void process (const float* in, float* out, int n, const EngineParams& p)
    {
        const bool studio = p.latency == 1;
        const auto mode = studio ? Voice::Studio : (p.algo == 1 ? Voice::LiveChords : Voice::Classic);
        for (auto& v : voices) v.setMode (mode, p.model == ModelBass);
        tracker.setGlitch (p.glitch);
        const int studioDelay = studio ? latencySamples (p) : 0;

        const bool anchorOn = p.anchor == 1 || (p.anchor == 0 && (p.input == 1 || p.model == ModelNeon));
        anchorIn.set (sr, p.anchorHz);
        anchorOut.set (sr, p.anchorHz);
        if (anchorOn != anchorWasOn) { anchorIn.reset(); anchorOut.reset(); anchorWasOn = anchorOn; }
        const bool bandLimit = p.origBandwidth && p.algo == 1 && p.section == 1 && ! studio;

        if (p.treadle != lastTreadleParam)
        {
            lastTreadleParam = p.treadle;
            treadleG.to (snapEnds (p.treadle), (int) (0.01 * sr));
        }
        if (p.section != lastSection)
        {
            // switching section lets go of anything held, so nothing sticks
            lastSection = p.section;
            mom.to (0, (int) (p.fall * sr)); layerHeld = false; dropHeld = false; p1Held = p2Held = false; glideHeld = false;
        }

        const float outGain = dbToGain (p.levelDb);
        int ev = 0;
        for (int start = 0; start < n; start += kChunk)
        {
            const int len = std::min (kChunk, n - start);
            while (ev < nEvents && events[(size_t) ev].offset < start + len) handle (events[(size_t) ev++], p);

            updateControl (p, len);
            const Plan plan = makePlan (p);
            for (int v = 0; v < kVoices; ++v) voiceGain[(size_t) v].to (plan.gain[v], len);
            const float wetTarget = p.bypass ? 0.0f : 1.0f;
            if (wetTarget != wet.target) wet.to (wetTarget, (int) ((p.bypass && p.trails ? 0.25 : 0.01) * sr));

            // Classic lines its grains up with the note's cycles
            const float period = tracker.periodSamples();
            const float minGrain = (float) ((p.model == ModelBass ? 0.006 : 0.0035) * sr);
            const float grain = period > 0.0f ? period * std::ceil (minGrain / period) : minGrain;
            // ignore jitter under 1.5%: it only roughens the splices. Real mistakes, an octave
            // out, still get through, and they are the original's warble
            if (std::abs (grain - grainSet) > grainSet * 0.015f)
            {
                grainSet = grain;
                for (auto& v : voices) v.setClassicGrain (grain);
            }
            tracker.rngJitter = rng.next();

            for (int i = start; i < start + len; ++i)
            {
                const float x = in[i];
                tracker.push (x);
                dry[(size_t) dw] = x;
                const float xd = dry[(size_t) ((dw - studioDelay) & dmask)];
                dw = (dw + 1) & dmask;

                const float w = wet.next();
                const float bendBack = p.trails ? w : 1.0f;   // with trails the pitch slides home as it fades
                float y = plan.dryGain * xd;
                for (int v = 0; v < kVoices; ++v)
                {
                    const float g = voiceGain[(size_t) v].next();
                    if (g <= 0.0f && plan.gain[v] <= 0.0f && voices[(size_t) v].idle())
                    {
                        voices[(size_t) v].process (x, 0.0f, true);
                        continue;
                    }
                    y += g * voices[(size_t) v].process (x, plan.semis[v] * bendBack, plan.allowDry[v]);
                }
                if (bandLimit) y = bw[1].process (bw[0].process (y));
                if (anchorOn)
                {
                    // the low end stays dry and on time, the shifted signal sits above it
                    y = anchorOut.high (y) + anchorIn.low (xd);
                }
                out[i] = (xd + (y - xd) * w) * outGain;
            }
            uiSemis.store (plan.semis[0] * (plan.gain[0] > 0 ? 1.0f : 0.0f));
        }
        nEvents = 0;

        float pin = 0, pout = 0;
        for (int i = 0; i < n; ++i) { pin = std::max (pin, std::abs (in[i])); pout = std::max (pout, std::abs (out[i])); }
        uiInput.store (pin); uiOutput.store (pout);
        uiFreq.store (tracker.frequency());
        // roughly how long the shifted note takes to arrive right now, for the panel
        float delay;
        if (studio) delay = (float) studioDelay;
        else if (p.algo == 1) delay = tracker.frequency() > 0.0f && tracker.frequency() < 250.0f ? (float) (0.0095 * sr) : (float) (0.0045 * sr);
        else delay = grainSet * 0.5f + 2.0f;
        uiDelayMs.store (delay * 1000.0f / (float) sr);
        uiTreadle.store (effectiveTreadle);
    }

    // ---------------- what the panel and Pebble show ----------------
    std::atomic<float> uiSemis { 0 }, uiTreadle { 0 }, uiInput { 0 }, uiOutput { 0 }, uiFreq { 0 }, uiGlide { 0 }, uiDelayMs { 0 };
    std::atomic<int> uiHits { 0 }, uiSeqStep { 0 }, uiImpNext { 0 };
    std::atomic<bool> uiLayerActive { false }, uiDropActive { false }, uiP1 { false }, uiP2 { false }, uiGlideActive { false }, uiDetune { false };

private:
    enum EvKind { EvDown, EvUp, EvTreadle };
    struct Event { EvKind kind; int sw; float v; int offset; };
    struct Plan
    {
        float semis[kVoices] {};
        float gain[kVoices] {};
        bool allowDry[kVoices] { true, true, true, true };
        float dryGain = 0.0f;
    };

    void addEvent (const Event& e)
    {
        if (nEvents < kMaxEvents) events[(size_t) nEvents++] = e;
    }

    static float snapEnds (float v)
    {
        // the last 2% at each end is exactly heel or toe, so toe down is a true interval
        if (v < 0.02f) return 0.0f;
        if (v > 0.98f) return 1.0f;
        return (v - 0.02f) / 0.96f;
    }

    // ---------------- footswitches ----------------
    void handle (const Event& e, const EngineParams& p)
    {
        const long long at = now;   // chunk resolution, under half a millisecond
        if (e.kind == EvTreadle)
        {
            // glide between MIDI values over the gap they arrived in, so a sweep has no steps and no lag
            const long long gap = at - lastTreadleEvent;
            lastTreadleEvent = at;
            const int len = (int) std::max<long long> ((long long) (0.002 * sr), std::min<long long> (gap, (long long) (0.03 * sr)));
            treadleG.to (snapEnds (e.v), len);
            return;
        }
        const int sw = e.sw;
        if (sw < 0 || sw > 3) return;
        if (e.kind == EvDown)
        {
            if (down[(size_t) sw]) return;
            down[(size_t) sw] = true;
            cancelled[(size_t) sw] = false;
            downAt[(size_t) sw] = at;
            // a second switch within the combo window: both presses are taken back
            for (int partner : { sw ^ 1, sw == 1 ? 2 : (sw == 2 ? 1 : -1) })
            {
                if (partner < 0 || ! down[(size_t) partner] || cancelled[(size_t) partner]) continue;
                if ((double) (at - downAt[(size_t) partner]) < kCombo * sr)
                {
                    undoPress (partner, p);
                    cancelled[(size_t) partner] = cancelled[(size_t) sw] = true;
                    return;
                }
            }
            press (sw, p);
        }
        else
        {
            if (! down[(size_t) sw]) return;
            down[(size_t) sw] = false;
            if (cancelled[(size_t) sw]) return;
            release (sw, p, (double) (at - downAt[(size_t) sw]) / sr);
        }
    }

    void press (int sw, const EngineParams& p)
    {
        if (p.section == 0)
        {
            if (sw == 1) mom.to (1, std::max (1, (int) (p.rise * sr)));
            if (sw == 3) layerHeld = true;      // instant: tap latches on release, hold is momentary
            return;
        }
        if (sw == 1)
        {
            if (p.jump == 1) { glideHeld = true; if (p.glideLatch) glideLatched = ! glideLatched; }
            else { p1Held = true; trillPhase = 0; hit(); }
        }
        else if (sw == 2)
        {
            const int len = seqLength (p.hamMode);
            if (len > 0)
            {
                if (p.trill)
                {
                    if (p.retrigger == 2) seqRunning = ! seqRunning;
                    else { seqRunning = true; seqStep = 0; }
                    seqAuto = 0;
                }
                else seqStep = (seqStep + 1) % len;
                hit();
            }
            else
            {
                p2Held = true; trillPhase = 0;
                if (p.hamMode == HamImpossible) { impUseB = impNext; impNext = ! impNext; }
                hit();
            }
        }
        else if (sw == 3)
        {
            if (p.dropLatch) dropLatched = ! dropLatched;
            else dropHeld = true;
            hit();
        }
    }

    void undoPress (int sw, const EngineParams& p)
    {
        if (p.section == 0)
        {
            if (sw == 1) mom.to (0, std::max (1, (int) (p.fall * sr)));
            if (sw == 3) layerHeld = false;
            return;
        }
        if (sw == 1)
        {
            if (p.jump == 1) { glideHeld = false; if (p.glideLatch) glideLatched = ! glideLatched; }
            else p1Held = false;
        }
        else if (sw == 2)
        {
            const int len = seqLength (p.hamMode);
            if (len > 0)
            {
                if (p.trill) { if (p.retrigger == 2) seqRunning = ! seqRunning; else seqRunning = false; }
                else seqStep = (seqStep + len - 1) % len;
            }
            else
            {
                p2Held = false;
                if (p.hamMode == HamImpossible) impNext = ! impNext;
            }
        }
        else if (sw == 3)
        {
            if (p.dropLatch) dropLatched = ! dropLatched;
            else dropHeld = false;
        }
    }

    void release (int sw, const EngineParams& p, double heldSeconds)
    {
        if (p.section == 0)
        {
            if (sw == 1) mom.to (0, std::max (1, (int) (p.fall * sr)));
            if (sw == 3)
            {
                layerHeld = false;
                if (heldSeconds < kHold) layerLatched = ! layerLatched;   // a tap latches
            }
            return;
        }
        if (sw == 1) { if (p.jump == 1) glideHeld = false; else p1Held = false; }
        else if (sw == 2)
        {
            p2Held = false;
            if (seqLength (p.hamMode) > 0 && p.trill && p.retrigger == 1) seqRunning = false;
        }
        else if (sw == 3) dropHeld = false;
    }

    void hit() { uiHits.fetch_add (1, std::memory_order_relaxed); }

    // ---------------- per chunk control ----------------
    void updateControl (const EngineParams& p, int len)
    {
        now += len;
        treadleG.advance (len);
        mom.advance (len);
        effectiveTreadle = std::max (treadleG.value, mom.value);

        // shift layer: latched, flipped while D is held (the DT's momentary, inversion included)
        const bool layerActive = p.section == 0 ? (layerLatched != layerHeld) : layerLatched;
        const auto& L = layerTable()[(size_t) std::max (0, std::min (kLayerSteps - 1, p.layerStep))];
        const float layerTarget = (layerActive && p.layerType == 0) ? L.semis : 0.0f;
        if (layerTarget != layerG.target) layerG.to (layerTarget, std::max (1, (int) (p.layerGlide * sr)));
        layerG.advance (len);
        layerDryNow = layerActive && p.layerType == 0 && L.dry;
        layerDetuneNow = layerActive && p.layerType == 1;
        uiLayerActive.store (layerActive);

        // the drop: momentary flips the latched state while held, so latching it on makes D a kill switch
        dropActiveNow = p.dropLatch ? dropLatched : (dropLatched != dropHeld);
        uiDropActive.store (dropActiveNow);

        // glide: shift towards the interval while engaged, return when let go, at their own rates
        const bool glideActive = p.glideLatch ? glideLatched : (glideLatched != glideHeld);
        const auto& G = glideTable()[(size_t) std::max (0, std::min (kGlideSteps - 1, p.glideStep))];
        const float gTarget = glideActive ? (p.glideDown ? -G.semis : G.semis) : 0.0f;
        if (gTarget != glideG.target)
        {
            const float secs = glideActive ? p.glideShift : p.glideReturn;
            const float dist = std::abs (gTarget - glideG.value);
            const float full = std::max (1.0f, G.semis);
            glideG.to (gTarget, std::max (1, (int) (secs * sr * dist / full)));
        }
        glideG.advance (len);
        glideDryNow = glideActive && G.dry;
        uiGlide.store (glideG.value);
        uiGlideActive.store (glideActive);

        // trill: rapid presses at the set rate, or a sequence stepping on its own
        double rate = p.trillHz;
        if (p.trillSync)
        {
            static const double perBeat[5] = { 2.0, 3.0, 4.0, 6.0, 8.0 };
            rate = p.bpm / 60.0 * perBeat[std::max (0, std::min (4, p.trillDiv))];
        }
        const double step = rate * (double) len / sr;
        trillOn = true;
        if (p.trill && (p1Held || p2Held))
        {
            const double before = trillPhase;
            trillPhase += step;
            if (std::floor (trillPhase * 2.0) != std::floor (before * 2.0))
            {
                if (p2Held && p.hamMode == HamImpossible) { impUseB = ! impUseB; hit(); }
                else if (std::fmod (trillPhase, 1.0) < 0.5) hit();
            }
            if (! (p2Held && p.hamMode == HamImpossible)) trillOn = std::fmod (trillPhase, 1.0) < 0.5;
        }
        const int len5 = seqLength (p.hamMode);
        if (len5 > 0)
        {
            if (seqStep >= len5) seqStep = 0;
            if (p.trill && seqRunning)
            {
                seqAuto += step;
                if (seqAuto >= 1.0)
                {
                    seqAuto -= 1.0;
                    if (seqStep + 1 >= len5)
                    {
                        if (p.retrigger == 0) seqRunning = false;   // play once
                        else { seqStep = 0; hit(); }
                    }
                    else { ++seqStep; hit(); }
                }
            }
        }
        else seqRunning = false;
        uiSeqStep.store (seqStep);
        uiImpNext.store (impNext ? 1 : 0);
        uiP1.store (p1Held);
        uiP2.store (p2Held);
    }

    Plan makePlan (const EngineParams& p)
    {
        Plan plan;
        if (p.section == 0)
        {
            const auto& tab = tableFor (p.model);
            const auto& s = tab.s[(size_t) std::max (0, std::min (tab.count - 1, p.setting))];
            const float t = effectiveTreadle;
            const float layer = layerG.value;
            const bool layerShift = std::abs (layer) > 0.001f;
            // the "dry" under a harmony is the drop-tuned signal when the layer is on
            auto dryVoice = [&]
            {
                if (layerShift) { plan.semis[2] = layer; plan.gain[2] = 1.0f; }
                else plan.dryGain += 1.0f;
            };
            switch (s.kind)
            {
                case Kind::Bend:
                    plan.semis[0] = s.a * t + layer; plan.gain[0] = 1.0f;
                    break;
                case Kind::Harmony:
                    dryVoice();
                    plan.semis[0] = s.a + (s.b - s.a) * t + layer; plan.gain[0] = 1.0f; plan.allowDry[0] = false;
                    break;
                case Kind::Octave:
                    dryVoice();
                    plan.semis[0] = s.a + layer; plan.gain[0] = 1.0f - t; plan.allowDry[0] = false;
                    plan.semis[1] = s.b + layer; plan.gain[1] = t; plan.allowDry[1] = false;
                    break;
                case Kind::Detune:
                    dryVoice();
                    plan.semis[0] = s.a * 0.01f + layer; plan.gain[0] = 0.8f * t; plan.allowDry[0] = false;
                    if (s.b != 0.0f) { plan.semis[1] = s.b * 0.01f + layer; plan.gain[1] = 0.6f * t; plan.allowDry[1] = false; }
                    break;
            }
            if (layerDryNow) plan.dryGain += 1.0f;
            if (layerDetuneNow) { plan.semis[3] = -0.10f; plan.gain[3] = 0.55f; plan.allowDry[3] = false; }
            uiDetune.store (s.kind == Kind::Detune && t > 0.05f);
            return plan;
        }

        // stomp section: drop, then the jumps on top
        const auto& D = dropTable()[(size_t) std::max (0, std::min (kDropSteps - 1, p.dropStep))];
        const float base = dropActiveNow ? D.semis : 0.0f;
        float jump = glideG.value;
        bool jumping = std::abs (glideG.value) > 0.01f;
        const int len = seqLength (p.hamMode);
        if (len > 0)
        {
            const auto& seq = len == 2 ? p.seq2 : (len == 3 ? p.seq3 : p.seq5);
            jump += (float) seq[(size_t) std::min (seqStep, len - 1)];
            jumping = jumping || seq[(size_t) std::min (seqStep, len - 1)] != 0;
        }
        if (p1Held && trillOn) { jump += (float) p.p1; jumping = true; }
        if (p2Held && len == 0)
        {
            const bool on = p.hamMode == HamImpossible ? true : trillOn;
            if (on) { jump += (float) (p.hamMode == HamImpossible && impUseB ? p.p2b : p.p2); jumping = true; }
        }
        plan.semis[0] = base + jump; plan.gain[0] = 1.0f;
        if (dropActiveNow && D.dry) plan.dryGain += 1.0f;
        if (p.dryPlus && jumping) plan.dryGain += 1.0f;
        if (glideDryNow) plan.dryGain += 1.0f;
        uiDetune.store (false);
        return plan;
    }

    double sr = 48000;
    int studioFrame = 1024;
    Tracker tracker;
    std::array<Voice, kVoices> voices;
    std::array<Glide, kVoices> voiceGain;
    std::vector<float> dry;
    int dmask = 0, dw = 0;
    Crossover anchorIn, anchorOut;
    bool anchorWasOn = false;
    Biquad bw[2];
    Rng rng;

    Glide treadleG, mom, layerG, glideG, wet;
    float effectiveTreadle = 0, lastTreadleParam = -1, grainSet = 1.0f;
    long long now = 0, lastTreadleEvent = -100000;
    std::array<Event, kMaxEvents> events {};
    int nEvents = 0, lastSection = -1;

    std::array<bool, 4> down {}, cancelled {};
    std::array<long long, 4> downAt {};
    bool layerLatched = false, layerHeld = false, dropLatched = false, dropHeld = false;
    bool glideLatched = false, glideHeld = false, p1Held = false, p2Held = false;
    bool impNext = false, impUseB = false, seqRunning = false, trillOn = true;
    bool layerDryNow = false, layerDetuneNow = false, dropActiveNow = false, glideDryNow = false;
    int seqStep = 0;
    double trillPhase = 0, seqAuto = 0;
};
} // namespace db
