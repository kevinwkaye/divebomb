#pragma once

#include "Params.h"
#include "PedalControl.h"
#include "Keeper.h"
#include "engine/DivebombEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class DivebombProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DivebombProcessor();
    ~DivebombProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Divebomb"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.3; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    db::Engine& getEngine() { return engine; }

    // ---- called from the panel (message thread) ----
    void footDown (int sw);   // the on-screen footswitches act exactly like the real ones
    void footUp (int sw);
    void stepSetting (int direction);
    void stepModel();
    void stepChoice (const char* id, int direction, bool wrap = false);
    void editPitch2 (int direction);     // in a sequence mode it edits the current step
    void tapTrill();
    int sequenceStep() const { return engine.uiSeqStep.load(); }

    // footswitch and expression setup, shared with every Flip Fella plugin
    PedalSettings& pedals() { return settings; }
    void learnFootswitch (int sw);
    void learnExpression();
    void calibrateExpression();
    void resetFootswitches();

    // Keeper
    void keepNow();
    void addKeeperNote (int number, const juce::String& note) { keeper.addNote (number, note); }
    juce::File exportKeepers();
    void openKeepersFolder();
    void learnKeeper();
    bool keeperLive() const { return settings.keeperLive; }
    void setKeeperLive (bool live);
    int lastKeeper() const { return keeper.lastNumber(); }

    // footswitch MIDI read straight from the device, so every copy works without a MIDI track
    void handleDirectMidi (const juce::MidiMessage&);   // MIDI thread
    bool pedalsDirect() const { return direct->connected(); }
    juce::String footInputStatus() const { return direct->status(); }
    juce::String footDevice() const { return settings.footDevice; }
    void setFootDevice (const juce::String& device);
    std::atomic<bool> hostFeedsPedals { false };
    bool isLearning() const { return learnTarget != LearnNone; }
    int learningWhat() const { return learnTarget; }
    bool isCalibrating() const { return calibrating; }
    enum { LearnNone = -1, LearnExpression = 4, LearnKeeper = 5 };

    // ---- what the panel and Pebble show ----
    float expressionLevel() const { return uiExp.load(); }
    double lastExpressionMove() const { return expMovedAt; }
    int reportedLatency() const { return latencyNow.load(); }
    std::atomic<int> uiFootPulse { 0 }, uiKeepPulse { 0 }, uiFootDown { 0 };
    juce::String uiMessage;
    double uiMessageAt = 0.0;
    void say (const juce::String& s);

private:
    void timerCallback() override;
    db::EngineParams readParams();
    void handleMidi (const juce::MidiMessage&, bool fromDevice, int offset);
    void onFootEvent (int sw, FootswitchLogic::Event);
    void setNorm (const juce::String& id, float v01);
    void setPlain (const juce::String& id, float plain);
    void toggle (const char* id);

    db::Engine engine;
    Keeper keeper;
    std::vector<float> monoIn, monoOut;
    std::map<juce::String, std::atomic<float>*> raw;
    float f (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }
    bool on (const char* id) const { return f (id) > 0.5f; }
    int idx (const char* id) const { return (int) std::lround (f (id)); }

    // footswitch and expression messages for the audio thread, from the MIDI thread and the panel
    struct AudioMsg { int kind = 0; int a = 0; float v = 0; };
    enum { AFootDown, AFootUp, ATreadle };
    juce::AbstractFifo audioFifo { 256 };
    std::array<AudioMsg, 256> audioMsgs;
    juce::SpinLock audioWriteLock;
    void toAudio (int kind, int a, float v);

    // MIDI handed to the message thread
    struct Pending { int kind = 0; int a = 0; float v = 0; };
    enum PendingKind { FootPress, FootRelease, Expression, Learned, KeepCC, ProgramChange };
    juce::AbstractFifo fifo { 512 };
    std::array<Pending, 512> pending;
    juce::AbstractFifo directFifo { 512 };
    std::array<Pending, 512> directPending;
    juce::SpinLock directWriteLock;
    juce::SharedResourcePointer<DirectMidiInput> direct;
    void queue (int kind, int a, float v);
    juce::Time settingsStamp;
    bool wasDirect = false;
    double nextSettingsCheck = 0.0;

    PedalSettings settings;
    FootswitchLogic footswitches;
    std::atomic<int> learnTarget { LearnNone };
    bool calibrating = false;
    double calibrateUntil = 0.0;
    int calLo = 127, calHi = 0, calFirst = -1;
    double expMovedAt = 0.0;
    std::atomic<float> uiExp { 0.0f };
    std::atomic<int> expRouteTreadle { 1 };   // the expression pedal drives the treadle on the audio thread

    // latched states the footswitches change on the audio thread, mirrored to parameters
    static constexpr const char* latchIds[3] = { ParamIDs::layerOn, ParamIDs::dropOn, ParamIDs::glideOn };
    std::array<float, 3> latchSeen { -1, -1, -1 };
    std::array<std::atomic<int>, 3> latchReq {};
    std::atomic<int> latencyNow { 0 };
    std::atomic<double> hostBpm { 120.0 };
    bool aBypassFlipped = false;
    std::vector<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DivebombProcessor)
};
