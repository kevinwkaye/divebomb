#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several Divebomb windows can be open at once. Footswitches drive one of them at a time:
// the one whose panel was last touched, or the next one along when C+D are tapped.
namespace
{
SpinLock focusLock;
Array<DivebombProcessor*> instances;
std::atomic<DivebombProcessor*> focused { nullptr };

bool hasFocus (DivebombProcessor* p)
{
    auto* f = focused.load();
    return f == nullptr || f == p;
}

thread_local bool fromDeviceNow = false;

void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    DivebombProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}

double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
} // namespace

DivebombProcessor::DivebombProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DivebombState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    for (auto& r : latchReq) r.store (-1);
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

DivebombProcessor::~DivebombProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void DivebombProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    keeper.prepare (sampleRate);
    monoIn.assign ((size_t) jmax (16, samplesPerBlock), 0.0f);
    monoOut.assign ((size_t) jmax (16, samplesPerBlock), 0.0f);
    std::fill (latchSeen.begin(), latchSeen.end(), -1.0f);
}

bool DivebombProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

db::EngineParams DivebombProcessor::readParams()
{
    using namespace ParamIDs;
    db::EngineParams p;
    p.section = idx (section);
    p.model = idx (model);
    p.setting = idx (setting);
    p.algo = p.section == 1 ? idx (algoStomp) : idx (algo);
    p.latency = idx (latency);
    p.treadle = f (treadle);
    p.rise = f (rise); p.fall = f (fall);
    p.layerOn = on (layerOn); p.layerType = idx (layerType); p.layerStep = idx (layerStep); p.layerGlide = f (layerGlide);
    p.dropStep = idx (dropStep); p.dropLatch = idx (dropLatch) == 1; p.dropOn = on (dropOn);
    p.jump = idx (jump);
    p.p1 = db::hammerSemis (idx (p1)); p.p2 = db::hammerSemis (idx (p2)); p.p2b = db::hammerSemis (idx (p2b));
    p.hamMode = idx (hamMode);
    for (int i = 0; i < 2; ++i) p.seq2[(size_t) i] = db::hammerSemis (idx (seq2[i]));
    for (int i = 0; i < 3; ++i) p.seq3[(size_t) i] = db::hammerSemis (idx (seq3[i]));
    for (int i = 0; i < 5; ++i) p.seq5[(size_t) i] = db::hammerSemis (idx (seq5[i]));
    p.retrigger = idx (retrigger);
    p.dryPlus = on (dryPlus); p.trill = on (trill); p.trillSync = on (trillSync); p.trillDiv = idx (trillDiv); p.trillHz = f (trillHz);
    p.glideStep = idx (glideStep); p.glideDown = idx (glideDown) == 1; p.glideShift = f (glideShift); p.glideReturn = f (glideReturn);
    p.glideLatch = idx (glideLatch) == 1; p.glideOn = on (glideOn);
    p.anchor = idx (anchor); p.anchorHz = f (anchorHz); p.input = idx (input);
    p.glitch = f (glitch); p.origBandwidth = on (bandwidth); p.trails = on (trails); p.bypass = on (bypass);
    p.levelDb = f (level);
    p.bpm = hostBpm.load();
    return p;
}

void DivebombProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chIn = getTotalNumInputChannels(), chOut = getTotalNumOutputChannels();
    if (n == 0 || chOut == 0) return;
    if ((int) monoIn.size() < n) { monoIn.resize ((size_t) n); monoOut.resize ((size_t) n); }

    // footswitches and expression from the device and the panel, then from Ableton, in time order
    {
        const auto scope = audioFifo.read (audioFifo.getNumReady());
        auto run = [&] (int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto& m = audioMsgs[(size_t) i];
                if (m.kind == AFootDown) engine.footDown (m.a, 0);
                else if (m.kind == AFootUp) engine.footUp (m.a, 0);
                else engine.treadleCc (m.v, 0);
            }
        };
        run (scope.startIndex1, scope.blockSize1);
        run (scope.startIndex2, scope.blockSize2);
    }
    for (const auto meta : midi) handleMidi (meta.getMessage(), false, meta.samplePosition);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) hostBpm.store (*bpm);

    // latched states: the panel or a preset changed them, so the engine follows
    for (int i = 0; i < 3; ++i)
    {
        const float v = f (latchIds[i]);
        if (v != latchSeen[(size_t) i]) { engine.setLatched (i, v > 0.5f); latchSeen[(size_t) i] = v; }
    }

    const auto p = readParams();
    const float* L = buffer.getReadPointer (0);
    const float* R = chIn > 1 ? buffer.getReadPointer (1) : L;
    for (int i = 0; i < n; ++i) monoIn[(size_t) i] = chIn > 1 ? 0.5f * (L[i] + R[i]) : L[i];
    keeper.pushInput (L, R, n);

    engine.process (monoIn.data(), monoOut.data(), n, p);

    for (int c = 0; c < chOut; ++c) buffer.copyFrom (c, 0, monoOut.data(), n);
    keeper.pushOutput (buffer.getReadPointer (0), buffer.getReadPointer (chOut > 1 ? 1 : 0), n);

    // a footswitch changed a latched state: the panel and presets follow it
    for (int i = 0; i < 3; ++i)
        if (engine.latched (i) != (f (latchIds[i]) > 0.5f)) latchReq[(size_t) i].store (engine.latched (i) ? 1 : 0);
    latencyNow.store (engine.latencySamples (p));
}

// =====================================================================================
// MIDI: footswitches, expression and the original pedals' own program changes
// =====================================================================================
void DivebombProcessor::toAudio (int kind, int a, float v)
{
    const SpinLock::ScopedLockType l (audioWriteLock);
    const auto scope = audioFifo.write (1);
    if (scope.blockSize1 > 0) audioMsgs[(size_t) scope.startIndex1] = { kind, a, v };
}

void DivebombProcessor::queue (int kind, int a, float v)
{
    if (fromDeviceNow)
    {
        const auto scope = directFifo.write (1);
        if (scope.blockSize1 > 0) directPending[(size_t) scope.startIndex1] = { kind, a, v };
        return;
    }
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = { kind, a, v };
}

void DivebombProcessor::handleDirectMidi (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true, 0);
    fromDeviceNow = false;
}

void DivebombProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setWanted (device);
}

void DivebombProcessor::handleMidi (const MidiMessage& m, bool fromDevice, int offset)
{
    const int ch = m.getChannel();
    if (m.isProgramChange())
    {
        if (settings.pedalMapChannel != 0 && ch == settings.pedalMapChannel && (fromDevice || hasFocus (this)))
            queue (ProgramChange, m.getProgramChangeNumber(), 0.0f);
        return;
    }
    if (! m.isController()) return;
    const int cc = m.getControllerNumber(), val = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc, 0.0f);
        return;
    }

    // Straight from the footswitch device, any channel will do. Through Ableton, the channel must
    // match. When the device is read directly, the same messages arriving through Ableton are ignored.
    if (fromDevice || ! pedalsDirect())
    {
        int foot = -1;
        for (int k = 0; k < 4 && foot < 0; ++k)
            if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
        const bool keep = foot < 0 && settings.keeperCC >= 0 && (fromDevice || ch == settings.keeperChannel) && cc == settings.keeperCC;
        const bool expression = foot < 0 && ! keep && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
        if (foot >= 0 || keep || expression)
        {
            if (! fromDevice)
            {
                // the copy Ableton sends footswitch MIDI to takes charge, unless the copy in charge is fed too
                hostFeedsPedals.store (true);
                const SpinLock::ScopedLockType l (focusLock);
                auto* f = focused.load();
                if (f != this && (f == nullptr || ! f->hostFeedsPedals.load())) focused.store (this);
                if (focused.load() != this) return;
            }
            if (foot >= 0)
            {
                // pitch jobs happen right here on the audio thread, the rest on the message thread
                const bool down = val >= 64;
                if (fromDevice) toAudio (down ? AFootDown : AFootUp, foot, 0.0f);
                else if (down) engine.footDown (foot, offset);
                else engine.footUp (foot, offset);
                queue (down ? FootPress : FootRelease, foot, 0.0f);
            }
            else if (keep) { if (val >= 64) queue (KeepCC, 0, 0.0f); }
            else
            {
                if (expRouteTreadle.load() == 1 && ! calibrating)
                {
                    const float v = settings.expressionValue (val);
                    uiExp.store (v);
                    if (fromDevice) toAudio (ATreadle, 0, v);
                    else engine.treadleCc (v, offset);
                }
                queue (Expression, val, 0.0f);
            }
            return;
        }
    }

    // the original pedals' chart: CC 11 is the treadle on the chosen channel
    if (settings.pedalMapChannel != 0 && ch == settings.pedalMapChannel && cc == 11)
    {
        const float v = (float) val / 127.0f;
        if (fromDevice) toAudio (ATreadle, 0, v);
        else engine.treadleCc (v, offset);
    }
}

void DivebombProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v01));
        p->endChangeGesture();
    }
}

void DivebombProcessor::setPlain (const String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void DivebombProcessor::toggle (const char* id) { setPlain (id, on (id) ? 0.0f : 1.0f); }

void DivebombProcessor::say (const String& s)
{
    uiMessage = s;
    uiMessageAt = nowSeconds();
}

void DivebombProcessor::timerCallback()
{
    const double now = nowSeconds();
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && hasFocus (this)) say ("FOOTSWITCHES READY");
        }
    }

    const int target = idx (ParamIDs::expTarget);
    expRouteTreadle.store ((target == 1 || (target == 0 && idx (ParamIDs::section) == 0)) ? 1 : 0);

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case KeepCC: keepNow(); break;
                case ProgramChange:
                {
                    // the treadle pedals' chart: 1 to 21 Classic, 22 to 42 Classic bypassed,
                    // 43 to 63 Chords, 64 to 84 Chords bypassed
                    const int pc = q.a + 1;
                    if (pc >= 1 && pc <= 84)
                    {
                        const int group = (pc - 1) / 21, s = (pc - 1) % 21;
                        setPlain (ParamIDs::section, 0.0f);
                        setPlain (ParamIDs::setting, (float) jmin (s, db::tableFor (idx (ParamIDs::model)).count - 1));
                        setPlain (ParamIDs::algo, group >= 2 ? 1.0f : 0.0f);
                        setPlain (ParamIDs::bypass, (group == 1 || group == 3) ? 1.0f : 0.0f);
                    }
                    break;
                }
                case Expression:
                {
                    const int cc = q.a;
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = cc;
                        calLo = jmin (calLo, cc);
                        calHi = jmax (calHi, cc);
                        uiExp.store ((float) cc / 127.0f);
                        break;
                    }
                    const float v = settings.expressionValue (cc);
                    uiExp.store (v);
                    if (expRouteTreadle.load() == 1) break;   // already on the audio thread
                    const int t = target == 0 ? 2 : target;
                    if (t == 2)
                    {
                        if (on (ParamIDs::trillSync)) setPlain (ParamIDs::trillDiv, (float) jmin (4, (int) (v * 5.0f)));
                        else setNorm (ParamIDs::trillHz, v);
                    }
                    else if (t == 3) setNorm (ParamIDs::glideShift, v);
                    else if (t == 4) setNorm (ParamIDs::glideReturn, v);
                    else if (t == 5) setNorm (ParamIDs::level, v);
                    break;
                }
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " IS CC " + String (cc));
                    }
                    else if (what == LearnKeeper) { settings.keeperChannel = ch; settings.keeperCC = cc; say ("KEEPER IS CC " + String (cc)); }
                    else if (what == LearnExpression) { settings.expChannel = ch; settings.expCC = cc; say ("EXPRESSION IS CC " + String (cc)); }
                    settings.save();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    if (on (ParamIDs::keeper)) { setPlain (ParamIDs::keeper, 0.0f); keepNow(); }

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    for (int i = 0; i < 3; ++i)
    {
        const int r = latchReq[(size_t) i].exchange (-1);
        if (r >= 0 && (r == 1) != on (latchIds[i])) setPlain (latchIds[i], (float) r);
    }
    if (latencyNow.load() != getLatencySamples()) setLatencySamples (latencyNow.load());

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            settings.save();
            say ("EXPRESSION CALIBRATED");
        }
        else say ("NO MOVEMENT, TRY AGAIN");
    }
}

// =====================================================================================
// Footswitches. Pitch jobs (B, D, and C in the Stomp section) run in the engine. The rest is here.
//   Treadle:  A on/off, hold Classic/Chords   B momentary whammy   C next setting, hold previous
//             D shift layer, tap latches, hold is momentary
//   Stomp:    A on/off, hold Trill   B Pitch 1 or the glide   C Pitch 2   D the drop
//   Both:     A+B section   B+C held 1 s next model   C+D next Divebomb, held 1 s Keeper
// =====================================================================================
void DivebombProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD || e == E::ComboBC) uiFootPulse.fetch_add (1);

    switch (e)
    {
        case E::ComboHoldCD: focused.store (this); keepNow(); return;
        case E::ComboCD:
        {
            const SpinLock::ScopedLockType l (focusLock);
            const int i = instances.indexOf (this);
            auto* next = instances.isEmpty() ? nullptr : instances[(i + 1) % instances.size()];
            focused.store (next);
            if (next != nullptr) next->say ("PEDALS FOCUS HERE");
            return;
        }
        case E::ComboHoldBC: stepModel(); return;
        case E::ComboBC: return;   // the engine has already taken back both pitch jobs
        case E::ComboAB:
            if (aBypassFlipped) { toggle (ParamIDs::bypass); aBypassFlipped = false; }
            setPlain (ParamIDs::section, idx (ParamIDs::section) == 0 ? 1.0f : 0.0f);
            say (idx (ParamIDs::section) == 0 ? "TREADLE" : "STOMP");
            return;
        default: break;
    }

    const bool stomp = idx (ParamIDs::section) == 1;
    if (sw == 0)
    {
        if (e == E::Down) { toggle (ParamIDs::bypass); aBypassFlipped = true; }
        else if (e == E::HoldStart)
        {
            if (aBypassFlipped) toggle (ParamIDs::bypass);
            aBypassFlipped = false;
            if (stomp) { toggle (ParamIDs::trill); say (on (ParamIDs::trill) ? "TRILL ON" : "TRILL OFF"); }
            else
            {
                setPlain (ParamIDs::algo, idx (ParamIDs::algo) == 0 ? 1.0f : 0.0f);
                say (idx (ParamIDs::algo) == 0 ? "CLASSIC" : "CHORDS");
            }
        }
        else aBypassFlipped = false;
    }
    else if (sw == 2 && ! stomp)
    {
        if (e == E::Tap) stepSetting (1);
        else if (e == E::HoldStart) stepSetting (-1);
    }
}

void DivebombProcessor::footDown (int sw)
{
    focused.store (this);
    toAudio (AFootDown, sw, 0.0f);
    footswitches.press (sw, nowSeconds());
}

void DivebombProcessor::footUp (int sw)
{
    toAudio (AFootUp, sw, 0.0f);
    footswitches.release (sw, nowSeconds());
}

void DivebombProcessor::stepSetting (int direction)
{
    const int count = db::tableFor (idx (ParamIDs::model)).count;
    setPlain (ParamIDs::setting, (float) ((idx (ParamIDs::setting) + direction + count) % count));
}

void DivebombProcessor::stepModel()
{
    const int m = (idx (ParamIDs::model) + 1) % db::kModels;
    setPlain (ParamIDs::model, (float) m);
    say (db::modelName (m));
}

void DivebombProcessor::stepChoice (const char* id, int direction, bool wrap)
{
    auto* c = dynamic_cast<AudioParameterChoice*> (apvts.getParameter (id));
    if (c == nullptr) return;
    const int count = c->choices.size();
    int v = c->getIndex() + direction;
    v = wrap ? (v + count) % count : jlimit (0, count - 1, v);
    setPlain (id, (float) v);
}

void DivebombProcessor::editPitch2 (int direction)
{
    const int mode = idx (ParamIDs::hamMode);
    const int len = db::seqLength (mode);
    if (len == 0) { stepChoice (ParamIDs::p2, direction); return; }
    const int step = jlimit (0, len - 1, engine.uiSeqStep.load());
    stepChoice (len == 2 ? ParamIDs::seq2[step] : (len == 3 ? ParamIDs::seq3[step] : ParamIDs::seq5[step]), direction);
}

void DivebombProcessor::tapTrill()
{
    const double now = nowSeconds();
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 4) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double interval = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        setPlain (ParamIDs::trillSync, 0.0f);
        setPlain (ParamIDs::trillHz, (float) jlimit (2.0, 24.0, 1.0 / interval));
        say ("TRILL " + String (1.0 / interval, 1) + " HZ");
    }
}

void DivebombProcessor::learnFootswitch (int sw) { learnTarget.store (sw); say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw))); }
void DivebombProcessor::learnExpression() { learnTarget.store (LearnExpression); say ("MOVE THE EXPRESSION PEDAL"); }

void DivebombProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = nowSeconds() + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void DivebombProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.save();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

// =====================================================================================
// Keeper
// =====================================================================================
void DivebombProcessor::keepNow()
{
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "Divebomb");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("recreates", "DigiTech Whammy 5, Whammy DT, MonoNeon Whammy, Bass Whammy, The Drop, HammerOn, Whammy Ricochet");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    e->setProperty ("tempo", hostBpm.load());
    const int m = idx (ParamIDs::model);
    const auto& tab = db::tableFor (m);
    const auto& s = tab.s[(size_t) jlimit (0, tab.count - 1, idx (ParamIDs::setting))];
    e->setProperty ("preset", String (db::modelName (m)) + " " + s.name);
    e->setProperty ("section", idx (ParamIDs::section) == 0 ? "Treadle" : "Stomp");
    e->setProperty ("model", db::modelName (m));
    e->setProperty ("setting", String (s.group) + " " + s.name);
    e->setProperty ("shiftNowSemitones", engine.uiSemis.load());
    e->setProperty ("trackedHz", engine.uiFreq.load());
    e->setProperty ("latencySamples", latencyNow.load());

    DynamicObject::Ptr exp = new DynamicObject();
    exp->setProperty ("target", names::expTargets()[idx (ParamIDs::expTarget)]);
    exp->setProperty ("position", uiExp.load());
    exp->setProperty ("treadle", engine.uiTreadle.load());
    exp->setProperty ("movedSecondsAgo", nowSeconds() - expMovedAt);
    e->setProperty ("expression", var (exp.get()));

    DynamicObject::Ptr params = new DynamicObject();
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
        {
            if (rp->getParameterID() == ParamIDs::keeper) continue;
            DynamicObject::Ptr pv = new DynamicObject();
            pv->setProperty ("value", rp->convertFrom0to1 (rp->getValue()));
            pv->setProperty ("shown", rp->getCurrentValueAsText());
            params->setProperty (rp->getParameterID(), var (pv.get()));
        }
    e->setProperty ("parameters", var (params.get()));

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File DivebombProcessor::exportKeepers()
{
    const auto f = keeper.exportZip();
    if (f.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); f.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return f;
}

void DivebombProcessor::openKeepersFolder()
{
    Keeper::folder().createDirectory();
    Keeper::folder().startAsProcess();
}

void DivebombProcessor::learnKeeper() { learnTarget.store (LearnKeeper); say ("PRESS YOUR KEEPER SWITCH"); }
void DivebombProcessor::setKeeperLive (bool live) { settings.keeperLive = live; settings.save(); }

// =====================================================================================
// State
// =====================================================================================
void DivebombProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void DivebombProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* DivebombProcessor::createEditor() { return new DivebombEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DivebombProcessor(); }
