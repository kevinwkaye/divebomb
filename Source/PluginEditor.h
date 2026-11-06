#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Flip Fella palette. Divebomb's NEON model repaints the whole panel, so the colours can change.
namespace ff
{
inline juce::Colour bg, face, housing, card, cardHi, cardLow, cardInk, ink, dim, faint, line, ring, pin,
                    red, teal, coral, blob, blobShade, eye, faceInk, tapeA, tapeB, purple, blue, green, yellow;
void setNeon (bool neon);
bool isNeon();
juce::Font font (float height, float kerning = 0.04f);
juce::Font marker (float height);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
} // namespace ff

namespace ui
{
void pill (juce::Graphics&, juce::Rectangle<float> b, const juce::String& text, bool on, float size, juce::Colour onColour = ff::ink);

// A split-flap card that flips like the Karlsson clock when its value changes
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { if (onClick) onClick (e); }
    std::function<void (const juce::MouseEvent&)> onClick;

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f, textHeight = 34.0f;
};

class Pills : public juce::Component
{
public:
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int value() const { return selected; }

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0;
    float textSize;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

class ToggleChip : public juce::Component
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text,
                const juce::String& onText = {}, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String text, onText;
    float textSize;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

class ActionChip : public juce::Component
{
public:
    ActionChip (const juce::String& t, std::function<void()> fn, float textSize = 12.0f)
        : text (t), action (std::move (fn)), size (textSize) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override { pill (g, getLocalBounds().toFloat().reduced (1.0f), text, lit, size, custom ? fill : ff::ink); }
    void mouseDown (const juce::MouseEvent&) override { if (action) action(); }
    void setLit (bool l) { if (l != lit || custom) { lit = l; custom = false; repaint(); } }
    void setLit (bool l, juce::Colour c) { if (l != lit || c != fill || ! custom) { lit = l; fill = c; custom = true; repaint(); } }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }

private:
    juce::String text;
    std::function<void()> action;
    float size;
    bool lit = false, custom = false;
    juce::Colour fill;
};

class MenuChip : public juce::Component
{
public:
    MenuChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& prefix = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* param;
    juce::String prefix;
    int value = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// A knob drawn as a small clock face, its name above and value below
class Dial : public juce::Component
{
public:
    Dial (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, int knobSize = 64);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    int knobSize;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class LabelledSlider : public juce::Component
{
public:
    LabelledSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The hero dial: every setting of the current model round one Karlsson face, grouped by arc
class SettingDial : public juce::Component
{
public:
    explicit SettingDial (DivebombProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    int hitTest (juce::Point<float>) const;
    DivebombProcessor& proc;
};

// The treadle: drag it, and it rocks with the expression pedal and footswitch B
class TreadleView : public juce::Component
{
public:
    explicit TreadleView (DivebombProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { start = proc.apvts.getRawParameterValue ("treadle")->load(); }
    void mouseDrag (const juce::MouseEvent& e) override { drag ((float) e.getDistanceFromDragStartX() / (float) getWidth() - (float) e.getDistanceFromDragStartY() / 120.0f); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    void drag (float delta);
    DivebombProcessor& proc;
    float start = 0;
};

// The hammer pedal's pitch LEDs: major-scale intervals, two lit for the notes in between
class PitchLadder : public juce::Component
{
public:
    void setSemis (int s) { if (s != semis) { semis = s; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    int semis = 0;
};

// The glide pedal's trajectory ladder
class Trajectory : public juce::Component
{
public:
    void set (float frac, bool up, bool idle, double now);
    void paint (juce::Graphics&) override;

private:
    float lit = 0;
    bool goingUp = true;
};

// The big A to D buttons along the strip: they act exactly like the footswitches
class FootButton : public juce::Component
{
public:
    FootButton (DivebombProcessor& p, int s) : proc (p), sw (s) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.footDown (sw); }
    void mouseUp (const juce::MouseEvent&) override { proc.footUp (sw); }
    void setJob (const juce::String& a, const juce::String& b) { if (a != job1 || b != job2) { job1 = a; job2 = b; repaint(); } }

private:
    DivebombProcessor& proc;
    int sw;
    juce::String job1, job2;
};

class BigButton : public juce::Component
{
public:
    std::function<void()> onDown, onRightClick;
    void set (const juce::String& main, const juce::String& small, juce::Colour border);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }

private:
    juce::String mainText, smallText;
    juce::Colour borderColour = ff::line;
    bool pressed = false;
};

// One footswitch on the Pedal page: its jobs in both sections, what it listens for, Learn
class FootswitchCard : public juce::Component
{
public:
    FootswitchCard (DivebombProcessor&, int sw);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    DivebombProcessor& proc;
    int sw;
    FlipCard letter;
    ActionChip learn, reset;
    juce::String sends;
};

class ExpressionMeter : public juce::Component
{
public:
    explicit ExpressionMeter (DivebombProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    DivebombProcessor& proc;
};

class FootInputLabel : public juce::Component
{
public:
    explicit FootInputLabel (DivebombProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    DivebombProcessor& proc;
};

// Pebble: tall as the pitch rises, flat as it dives, wobbles on detune, jumps on every hit
class Pebble : public juce::Component
{
public:
    explicit Pebble (DivebombProcessor& p) : proc (p) {}
    void nod() { nodT = 1.0f; }
    void update (double now, double dt);
    void paint (juce::Graphics&) override;

private:
    DivebombProcessor& proc;
    float level = 0, stretch = 0, jump = 0, wobble = 0, lean = 0, nodT = 0;
    int lastHits = 0, lastPulse = 0;
    bool blinking = false, asleep = false;
    double nextBlink = 2.0, blinkUntil = 0, now = 0;
};

// Gaffer tape across the neon panel, labels in marker
class TapeLayer : public juce::Component
{
public:
    TapeLayer() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
};

// The flip-card wipe between the dark and neon looks
class Wipe : public juce::Component
{
public:
    Wipe() { setInterceptsMouseClicks (false, false); }
    void start (bool toNeon);
    bool running() const { return t >= 0.0f; }
    void advance (double dt);
    void paint (juce::Graphics&) override;
    std::function<void()> onSwap;

private:
    float t = -1.0f;
    bool neon = false, swapped = false;
};
} // namespace ui

class DivebombEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DivebombEditor (DivebombProcessor&);
    ~DivebombEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    enum class View { Main, Deep, Pedal };
    void timerCallback() override;
    void setView (View);
    void layoutSections();
    void keeperMenu();
    void closeNote (bool save);
    float raw (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    int idx (const char* id) const { return juce::roundToInt (raw (id)); }

    DivebombProcessor& proc;
    ui::FlipLookAndFeel lnf;
    double lastTick = 0;
    View view = View::Main;
    int lastSection = -1, lastModel = -1;

    // header
    ui::Pills models, sections, latency;
    ui::FlipCard latencyCard;
    ui::ActionChip viewMain, viewDeep, viewPedal;

    // treadle section
    ui::FlipCard groupCard, settingCard, pitchCard, layerCard;
    ui::SettingDial settingDial;
    ui::Pills algo, layerType, anchor, input;
    ui::TreadleView treadleView;
    ui::LabelledSlider riseSlider, fallSlider;
    ui::ToggleChip trails, layerOn;
    ui::ActionChip layerDown, layerUp;
    ui::Dial layerGlide, glitchDial, crossover;

    // stomp section
    ui::FlipCard dropCard, bCard, cCard, rateCard;
    ui::ActionChip dropLower, dropHigher;
    ui::Pills dropMode, jump, retrigger, algoStomp, glideDir, glideMode, anchor2;
    ui::ActionChip modeChip, p1Down, p1Up, p2Down, p2Up, p2bDown, p2bUp, glideLess, glideMore, tapChip;
    ui::ToggleChip dropOn, dryPlus, trill, trillSync, bandwidth;
    ui::MenuChip trillDiv;
    ui::Dial glideShift, glideReturn, trillHz;
    ui::PitchLadder ladder;
    ui::Trajectory trajectory;

    // deep page
    ui::Dial level, deepGlitch, deepRise, deepFall, deepLayerGlide, deepAnchorHz;
    ui::ToggleChip deepTrails, deepBandwidth;
    ui::MenuChip expTarget;

    // pedal page
    std::array<std::unique_ptr<ui::FootswitchCard>, 4> footCards;
    ui::ExpressionMeter expression;
    ui::FootInputLabel footInput;

    // strip
    ui::Pebble pebble;
    std::array<std::unique_ptr<ui::FootButton>, 4> foot;
    ui::BigButton keepButton, bypassButton;
    juce::TextEditor noteEditor;
    int noteNumber = 0, lastKeepPulse = 0;
    double keptUntil = 0.0, noteUntil = 0.0;
    juce::String mood, moodSub;

    ui::TapeLayer tape;
    ui::Wipe wipe;
    std::vector<juce::Component*> treadleOnly, stompOnly, mainOnly, deepOnly, pedalOnly;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DivebombEditor)
};
