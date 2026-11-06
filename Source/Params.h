#pragma once

#include "engine/Models.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Every control, footswitch action and Keeper action is a parameter in this one list,
// so a remote controller (the Pi rig's phone page) can read and set all of it.
namespace ParamIDs
{
inline constexpr const char* section = "section";
inline constexpr const char* model = "model";
inline constexpr const char* setting = "setting";
inline constexpr const char* algo = "algo";
inline constexpr const char* algoStomp = "algoStomp";
inline constexpr const char* latency = "latency";
inline constexpr const char* treadle = "treadle";
inline constexpr const char* rise = "rise";
inline constexpr const char* fall = "fall";
inline constexpr const char* layerOn = "layerOn";
inline constexpr const char* layerType = "layerType";
inline constexpr const char* layerStep = "layerStep";
inline constexpr const char* layerGlide = "layerGlide";
inline constexpr const char* dropStep = "dropStep";
inline constexpr const char* dropLatch = "dropLatch";
inline constexpr const char* dropOn = "dropOn";
inline constexpr const char* jump = "jump";
inline constexpr const char* p1 = "pitch1";
inline constexpr const char* p2 = "pitch2";
inline constexpr const char* p2b = "pitch2b";
inline constexpr const char* hamMode = "hamMode";
inline constexpr const char* seq2[2] = { "seq2a", "seq2b" };
inline constexpr const char* seq3[3] = { "seq3a", "seq3b", "seq3c" };
inline constexpr const char* seq5[5] = { "seq5a", "seq5b", "seq5c", "seq5d", "seq5e" };
inline constexpr const char* retrigger = "retrigger";
inline constexpr const char* dryPlus = "dryPlus";
inline constexpr const char* trill = "trill";
inline constexpr const char* trillSync = "trillSync";
inline constexpr const char* trillDiv = "trillDiv";
inline constexpr const char* trillHz = "trillHz";
inline constexpr const char* glideStep = "glideStep";
inline constexpr const char* glideDown = "glideDown";
inline constexpr const char* glideShift = "glideShift";
inline constexpr const char* glideReturn = "glideReturn";
inline constexpr const char* glideLatch = "glideLatch";
inline constexpr const char* glideOn = "glideOn";
inline constexpr const char* anchor = "anchor";
inline constexpr const char* anchorHz = "anchorHz";
inline constexpr const char* input = "input";
inline constexpr const char* glitch = "glitch";
inline constexpr const char* bandwidth = "bandwidth";
inline constexpr const char* trails = "trails";
inline constexpr const char* bypass = "bypass";
inline constexpr const char* level = "level";
inline constexpr const char* expTarget = "expTarget";
inline constexpr const char* keeper = "keeper";
} // namespace ParamIDs

namespace names
{
inline juce::StringArray layerSteps()
{
    juce::StringArray a;
    for (const auto& s : db::layerTable()) a.add (s.name);
    return a;
}
inline juce::StringArray dropSteps()
{
    juce::StringArray a;
    for (const auto& s : db::dropTable()) a.add (s.name);
    return a;
}
inline juce::StringArray glideSteps()
{
    juce::StringArray a;
    for (const auto& s : db::glideTable()) a.add (s.name);
    return a;
}
inline juce::String pitchName (int semis)
{
    if (semis == 24) return "+2 OCT";
    if (semis == -24) return "-2 OCT";
    if (semis == 12) return "+OCT";
    if (semis == -12) return "-OCT";
    if (semis == 0) return "ROOT";
    return (semis > 0 ? "+" : "") + juce::String (semis);
}
inline juce::StringArray hammerPitches()
{
    juce::StringArray a;
    for (int i = 0; i < db::kHammerSteps; ++i) a.add (pitchName (db::hammerSemis (i)));
    return a;
}
inline const juce::StringArray& hamModes() { static juce::StringArray a { "HAMMER", "IMPOSSIBLE", "SEQ 2", "SEQ 3", "SEQ 5" }; return a; }
inline const juce::StringArray& expTargets() { static juce::StringArray a { "Auto (treadle, or trill rate in Stomp)", "Treadle", "Trill rate", "Glide shift time", "Glide return time", "Level" }; return a; }
} // namespace names

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using namespace ParamIDs;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto choice = [&] (const char* id, const char* name, StringArray c, int def)
    { p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def)); };
    auto flag = [&] (const char* id, const char* name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto real = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit,
                     std::function<String (float, int)> text)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                            AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (std::move (text))));
    };
    auto ms = [] (float v, int) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; };
    auto skewed = [] (float lo, float hi, float mid) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (mid); return r; };

    choice (section, "Section", { "Treadle", "Stomp" }, 0);
    choice (model, "Model", { "Five", "Dual", "Neon", "Bass" }, 0);
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { setting, 1 }, "Setting", 0, db::kMaxSettings - 1, 1));
    choice (algo, "Treadle algorithm", { "Classic", "Chords" }, 0);
    choice (algoStomp, "Stomp algorithm", { "Classic", "Chords" }, 1);
    choice (latency, "Latency", { "Live", "Studio" }, 0);
    real (treadle, "Treadle", { 0.0f, 1.0f }, 0.0f, "", [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    real (rise, "Momentary rise", skewed (0.0f, 2.0f, 0.25f), 0.12f, "s", ms);
    real (fall, "Momentary fall", skewed (0.0f, 2.0f, 0.25f), 0.25f, "s", ms);
    flag (layerOn, "Shift layer on", false);
    choice (layerType, "Shift layer type", { "Drop tune", "Detune" }, 0);
    choice (layerStep, "Shift layer interval", names::layerSteps(), 10);
    real (layerGlide, "Shift layer glide", skewed (0.0f, 1.0f, 0.15f), 0.0f, "s", [] (float v, int) { return v < 0.001f ? String ("INSTANT") : String (roundToInt (v * 1000.0f)) + " ms"; });
    choice (dropStep, "Drop", names::dropSteps(), 1);
    choice (dropLatch, "Drop footswitch", { "Momentary", "Latch" }, 0);
    flag (dropOn, "Drop latched on", false);
    choice (jump, "Footswitch B", { "Hammer", "Glide" }, 0);
    choice (p1, "Pitch 1", names::hammerPitches(), db::hammerIndex (12));
    choice (p2, "Pitch 2", names::hammerPitches(), db::hammerIndex (7));
    choice (p2b, "Pitch 2 second", names::hammerPitches(), db::hammerIndex (-5));
    choice (hamMode, "Pitch 2 mode", { "Hammer", "Impossible", "Sequence 2", "Sequence 3", "Sequence 5" }, 0);
    const int s2[2] = { 0, -2 }, s3[3] = { 0, -2, -5 }, s5[5] = { 0, 3, 5, 7, 12 };
    for (int i = 0; i < 2; ++i) choice (seq2[i], ("Sequence 2 step " + String (i + 1)).toRawUTF8(), names::hammerPitches(), db::hammerIndex (s2[i]));
    for (int i = 0; i < 3; ++i) choice (seq3[i], ("Sequence 3 step " + String (i + 1)).toRawUTF8(), names::hammerPitches(), db::hammerIndex (s3[i]));
    for (int i = 0; i < 5; ++i) choice (seq5[i], ("Sequence 5 step " + String (i + 1)).toRawUTF8(), names::hammerPitches(), db::hammerIndex (s5[i]));
    choice (retrigger, "Sequence retrigger", { "Play once", "Retrigger", "Loop" }, 0);
    flag (dryPlus, "Dry+", false);
    flag (trill, "Trill", false);
    flag (trillSync, "Trill sync", true);
    choice (trillDiv, "Trill division", { "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 2);
    real (trillHz, "Trill rate", skewed (2.0f, 24.0f, 8.0f), 8.0f, "Hz", [] (float v, int) { return String (v, 1) + " HZ"; });
    choice (glideStep, "Glide interval", names::glideSteps(), 4);
    choice (glideDown, "Glide direction", { "Up", "Down" }, 0);
    real (glideShift, "Glide shift", skewed (0.0f, 3.0f, 0.3f), 0.04f, "s", ms);
    real (glideReturn, "Glide return", skewed (0.0f, 3.0f, 0.3f), 0.18f, "s", ms);
    choice (glideLatch, "Glide footswitch", { "Momentary", "Latch" }, 0);
    flag (glideOn, "Glide latched on", false);
    choice (anchor, "Low Anchor", { "Auto", "On", "Off" }, 0);
    real (anchorHz, "Low Anchor crossover", skewed (40.0f, 250.0f, 100.0f), 100.0f, "Hz", [] (float v, int) { return String (roundToInt (v)) + " HZ"; });
    choice (input, "Input", { "Guitar", "Bass" }, 0);
    real (glitch, "Glitch", { 0.0f, 2.0f }, 1.0f, "", [] (float v, int) { return v < 0.05f ? String ("CLEAN") : (std::abs (v - 1.0f) < 0.05f ? String ("ORIGINAL") : String (v, 2)); });
    flag (bandwidth, "Original bandwidth", true);
    flag (trails, "Trails", true);
    flag (bypass, "Bypass", false);
    real (level, "Level", { -24.0f, 12.0f }, 0.0f, "dB", [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " DB"; });
    choice (expTarget, "Expression moves", names::expTargets(), 0);
    flag (keeper, "Keeper", false);
    return { p.begin(), p.end() };
}
