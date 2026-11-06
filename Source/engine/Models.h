#pragma once

// The four treadle models and the footswitch pedals' tables, taken from the owner's manuals.
// Panel names are plain interval words: no brand or pedal names appear in the UI.

#include <array>

namespace db
{
enum class Kind { Bend, Harmony, Detune, Octave };

struct Setting
{
    const char* group;   // BEND, HYPER, DETUNE, HARMONY, OCTAVE
    const char* name;    // what the flip card shows
    Kind kind;
    float a, b;          // Bend: semitones at toe. Harmony and Octave: toe up, toe down. Detune: cents of the copy (b = optional second copy)
};

enum Model { ModelFive = 0, ModelDual = 1, ModelNeon = 2, ModelBass = 3, kModels = 4 };
inline const char* modelName (int m) { static const char* n[4] = { "FIVE", "DUAL", "NEON", "BASS" }; return n[m & 3]; }

constexpr int kMaxSettings = 21;

struct ModelTable
{
    int count;
    std::array<Setting, kMaxSettings> s;
};

// Whammy 5 and Whammy DT share the same 21 settings, in the order of the selector knob
inline const ModelTable& tableFive()
{
    static const ModelTable t { 21, { {
        { "BEND", "2 OCT UP", Kind::Bend, 24, 0 }, { "BEND", "1 OCT UP", Kind::Bend, 12, 0 },
        { "BEND", "5TH UP", Kind::Bend, 7, 0 }, { "BEND", "4TH UP", Kind::Bend, 5, 0 },
        { "BEND", "2ND DN", Kind::Bend, -2, 0 }, { "BEND", "4TH DN", Kind::Bend, -5, 0 },
        { "BEND", "5TH DN", Kind::Bend, -7, 0 }, { "BEND", "1 OCT DN", Kind::Bend, -12, 0 },
        { "BEND", "2 OCT DN", Kind::Bend, -24, 0 }, { "BEND", "DIVE BOMB", Kind::Bend, -36, 0 },
        { "DETUNE", "SHALLOW", Kind::Detune, -8, 0 }, { "DETUNE", "DEEP", Kind::Detune, -25, 0 },
        { "HARMONY", "OCT DN / OCT UP", Kind::Harmony, -12, 12 }, { "HARMONY", "5TH DN / 4TH DN", Kind::Harmony, -7, -5 },
        { "HARMONY", "4TH DN / MIN 3RD DN", Kind::Harmony, -5, -3 }, { "HARMONY", "5TH UP / 7TH UP", Kind::Harmony, 7, 10 },
        { "HARMONY", "5TH UP / 6TH UP", Kind::Harmony, 7, 9 }, { "HARMONY", "4TH UP / 5TH UP", Kind::Harmony, 5, 7 },
        { "HARMONY", "3RD UP / 4TH UP", Kind::Harmony, 4, 5 }, { "HARMONY", "MIN 3RD / 3RD UP", Kind::Harmony, 3, 4 },
        { "HARMONY", "2ND UP / 3RD UP", Kind::Harmony, 2, 4 } } } };
    return t;
}

inline const ModelTable& tableNeon()
{
    static const ModelTable t { 21, { {
        { "BEND", "2 OCT UP", Kind::Bend, 24, 0 }, { "BEND", "1 OCT UP", Kind::Bend, 12, 0 },
        { "BEND", "5TH UP", Kind::Bend, 7, 0 }, { "BEND", "4TH UP", Kind::Bend, 5, 0 },
        { "BEND", "2ND UP", Kind::Bend, 2, 0 }, { "BEND", "2ND DN", Kind::Bend, -2, 0 },
        { "BEND", "4TH DN", Kind::Bend, -5, 0 }, { "BEND", "5TH DN", Kind::Bend, -7, 0 },
        { "BEND", "1 OCT DN", Kind::Bend, -12, 0 }, { "BEND", "DIVE BOMB", Kind::Bend, -36, 0 },
        { "HYPER", "3 OCT UP", Kind::Bend, 36, 0 },
        { "DETUNE", "DETUNE", Kind::Detune, -14, 11 },
        { "HARMONY", "4TH UP / 5TH UP", Kind::Harmony, 5, 7 }, { "HARMONY", "3RD UP / 4TH UP", Kind::Harmony, 4, 5 },
        { "HARMONY", "5TH DN / 4TH DN", Kind::Harmony, -7, -5 }, { "HARMONY", "6TH DN / 5TH DN", Kind::Harmony, -9, -7 },
        { "HARMONY", "4TH DN / 5TH UP", Kind::Harmony, -5, 7 }, { "HARMONY", "5TH DN / 4TH UP", Kind::Harmony, -7, 5 },
        { "OCTAVE", "OCT UP / 2 OCT UP", Kind::Octave, 12, 24 }, { "OCTAVE", "OCT DN / OCT UP", Kind::Octave, -12, 12 },
        { "OCTAVE", "2 OCT DN / OCT DN", Kind::Octave, -24, -12 } } } };
    return t;
}

inline const ModelTable& tableBass()
{
    static const ModelTable t { 21, { {
        { "BEND", "2 OCT UP", Kind::Bend, 24, 0 }, { "BEND", "1 OCT UP", Kind::Bend, 12, 0 },
        { "BEND", "5TH UP", Kind::Bend, 7, 0 }, { "BEND", "4TH UP", Kind::Bend, 5, 0 },
        { "BEND", "2ND UP", Kind::Bend, 2, 0 }, { "BEND", "2ND DN", Kind::Bend, -2, 0 },
        { "BEND", "4TH DN", Kind::Bend, -5, 0 }, { "BEND", "5TH DN", Kind::Bend, -7, 0 },
        { "BEND", "1 OCT DN", Kind::Bend, -12, 0 }, { "BEND", "DIVE BOMB", Kind::Bend, -36, 0 },
        { "DETUNE", "SHALLOW", Kind::Detune, -6, 0 }, { "DETUNE", "DEEP", Kind::Detune, -20, 0 },
        { "HARMONY", "OCT UP / 2 OCT UP", Kind::Harmony, 12, 24 }, { "HARMONY", "OCT UP / 10TH UP", Kind::Harmony, 12, 16 },
        { "HARMONY", "OCT DN / OCT UP", Kind::Harmony, -12, 12 }, { "HARMONY", "OCT DN / 4TH DN", Kind::Harmony, -12, -5 },
        { "HARMONY", "5TH UP / OCT UP", Kind::Harmony, 7, 12 }, { "HARMONY", "5TH UP / 6TH UP", Kind::Harmony, 7, 9 },
        { "HARMONY", "5TH DN / 5TH UP", Kind::Harmony, -7, 7 }, { "HARMONY", "4TH DN / 5TH UP", Kind::Harmony, -5, 7 },
        { "HARMONY", "4TH DN / 3RD UP", Kind::Harmony, -5, 4 } } } };
    return t;
}

inline const ModelTable& tableFor (int model)
{
    switch (model)
    {
        case ModelNeon: return tableNeon();
        case ModelBass: return tableBass();
        default: return tableFive();
    }
}

// The shift layer: the DT's drop tune side, available in every model. Order of the DT's knob, top to bottom.
struct Step { const char* name; float semis; bool dry; };
constexpr int kLayerSteps = 18;
inline const std::array<Step, kLayerSteps>& layerTable()
{
    static const std::array<Step, kLayerSteps> t { {
        { "+OCT +DRY", 12, true }, { "+OCT", 12, false }, { "+7", 7, false }, { "+6", 6, false }, { "+5", 5, false },
        { "+4", 4, false }, { "+3", 3, false }, { "+2", 2, false }, { "+1", 1, false },
        { "-1", -1, false }, { "-2", -2, false }, { "-3", -3, false }, { "-4", -4, false }, { "-5", -5, false },
        { "-6", -6, false }, { "-7", -7, false }, { "-OCT", -12, false }, { "-OCT +DRY", -12, true } } };
    return t;
}

// The Drop: down only
constexpr int kDropSteps = 9;
inline const std::array<Step, kDropSteps>& dropTable()
{
    static const std::array<Step, kDropSteps> t { {
        { "-1", -1, false }, { "-2", -2, false }, { "-3", -3, false }, { "-4", -4, false }, { "-5", -5, false },
        { "-6", -6, false }, { "-7", -7, false }, { "-OCT", -12, false }, { "OCT +DRY", -12, true } } };
    return t;
}

// The glide pedal's seven intervals. The manual says only "7TH": read as the dominant 7th, as on the treadle models
constexpr int kGlideSteps = 7;
inline const std::array<Step, kGlideSteps>& glideTable()
{
    static const std::array<Step, kGlideSteps> t { {
        { "2ND", 2, false }, { "4TH", 5, false }, { "5TH", 7, false }, { "7TH", 10, false },
        { "OCT", 12, false }, { "2 OCT", 24, false }, { "OCT +DRY", 12, true } } };
    return t;
}

// The hammer pedal's 27 pitches: every semitone from an octave down to an octave up, plus two octaves either way
constexpr int kHammerSteps = 27;
inline int hammerSemis (int index)
{
    if (index <= 0) return -24;
    if (index >= 26) return 24;
    return index - 13;
}
inline int hammerIndex (int semis)
{
    if (semis <= -24) return 0;
    if (semis >= 24) return 26;
    return std::max (1, std::min (25, semis + 13));
}

// HammerOn's PITCH 2 modes, with their LED colours
enum HammerMode { HamHammer = 0, HamImpossible = 1, HamSeq2 = 2, HamSeq3 = 3, HamSeq5 = 4 };
inline int seqLength (int mode) { return mode == HamSeq2 ? 2 : (mode == HamSeq3 ? 3 : (mode == HamSeq5 ? 5 : 0)); }
} // namespace db
