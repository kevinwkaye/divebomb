#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette: the dark Flip Fella family, or the neon skin for the NEON model
// =====================================================================================
namespace ff
{
static bool neonNow = false;

void setNeon (bool neon)
{
    neonNow = neon;
    // flip cards stay black with white digits in both looks
    housing = Colour (neon ? 0xff101010 : 0xff060606);
    card = Colour (0xff1b1b1b); cardHi = Colour (0xff252525); cardLow = Colour (0xff141414); cardInk = Colour (0xfff3f0e8);
    pin = Colour (0xff3c3c3c); eye = Colours::white; faceInk = Colour (0xff0b0b12);
    tapeA = Colour (0xffff6a13); tapeB = Colour (0xffe9ff12);
    purple = Colour (0xffa46bff); blue = Colour (0xff4d8dff); green = Colour (0xff4fcf6a); yellow = Colour (0xfff2c230);
    if (neon)
    {
        bg = Colour (0xffe9ff12); face = Colour (0xffdcf200);
        ink = Colour (0xff101010); dim = Colour (0xff343800); faint = Colour (0xff5c6400);
        line = Colour (0xff101010); ring = Colour (0xff101010);
        red = Colour (0xffff4f0a); teal = Colour (0xff101010); coral = Colour (0xffff4f0a);
        blob = Colour (0xffff6a13); blobShade = Colour (0xffc24400);
    }
    else
    {
        bg = Colour (0xff050505); face = Colour (0xff0d0d0d);
        ink = Colour (0xfff3f0e8); dim = Colour (0xffa29e95); faint = Colour (0xff5a5852);
        line = Colour (0xff3a3a3a); ring = Colour (0xff1a1a1a);
        red = Colour (0xffe8392f); teal = Colour (0xff3fd6c8); coral = Colour (0xffff7a59);
        blob = Colour (0xff4d5bff); blobShade = Colour (0xff2329a8);
    }
}

bool isNeon() { return neonNow; }

Font font (float height, float kerning)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf, BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (typeface).withHeight (height).withKerningFactor (kerning));
}

Font marker (float height)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::PermanentMarkerRegular_ttf, BinaryData::PermanentMarkerRegular_ttfSize);
    return Font (FontOptions (typeface).withHeight (height));
}

void label (Graphics& g, const String& text, Rectangle<int> r, Justification j, Colour c, float size)
{
    g.setColour (c);
    g.setFont (font (size, 0.08f));
    g.drawText (text, r, j, false);
}
} // namespace ff

// the dark look is ready before any component picks up a colour
static const bool paletteReady = (ff::setNeon (false), true);

namespace ui
{
static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
static float rawOf (DivebombProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
static int idxOf (DivebombProcessor& p, const char* id) { return roundToInt (rawOf (p, id)); }

static void setParam (RangedAudioParameter* p, float plain)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}

void pill (Graphics& g, Rectangle<float> b, const String& text, bool on, float size, Colour onColour)
{
    const float r = b.getHeight() * 0.5f;
    if (on) { g.setColour (onColour); g.fillRoundedRectangle (b, r); }
    else { g.setColour (ff::line); g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f); }
    // text on a coloured fill is always near-black, on black it is the panel's background colour
    const bool darkText = on && onColour != ff::ink ? true : false;
    g.setColour (on ? (darkText ? Colour (0xff101010) : ff::bg) : ff::ink);
    g.setFont (ff::font (size, 0.06f));
    g.drawFittedText (text, b.reduced (4.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
}

// =====================================================================================
// FlipCard
// =====================================================================================
void FlipCard::setValue (const String& v)
{
    if (v == current) return;
    previous = current;
    current = v;
    progress = previous.isEmpty() ? 1.0f : 0.0f;
    repaint();
}

void FlipCard::advance (double dt)
{
    if (progress < 1.0f) { progress = jmin (1.0f, progress + (float) (dt / 0.26)); repaint(); }
}

void FlipCard::drawHalf (Graphics& g, Rectangle<float> card, const String& text, bool top, float scale, bool shade)
{
    const float hinge = card.getCentreY(), halfH = card.getHeight() * 0.5f;
    if (scale <= 0.001f) return;
    Graphics::ScopedSaveState save (g);
    auto clip = top ? Rectangle<float> (card.getX(), hinge - halfH * scale, card.getWidth(), halfH * scale)
                    : Rectangle<float> (card.getX(), hinge, card.getWidth(), halfH * scale);
    g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));
    if (top) g.setGradientFill (ColourGradient (ff::cardHi, 0, card.getY(), ff::card, 0, hinge, false));
    else     g.setGradientFill (ColourGradient (ff::card, 0, hinge, ff::cardLow, 0, card.getBottom(), false));
    g.fillRoundedRectangle (card, 7.0f);
    g.setColour (ff::cardInk);
    g.setFont (ff::font (textHeight, 0.02f));
    g.drawFittedText (text, card.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.6f);
    if (shade) { g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale))); g.fillRoundedRectangle (card, 7.0f); }
}

void FlipCard::paint (Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (outer, 10.0f);
    auto card = outer.reduced (6.0f);
    if (progress >= 1.0f) { drawHalf (g, card, current, true, 1.0f, false); drawHalf (g, card, current, false, 1.0f, false); }
    else
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, previous, false, 1.0f, false);
        const float eased = 1.0f - std::pow (1.0f - progress, 2.0f);
        if (eased < 0.5f) drawHalf (g, card, previous, true, 1.0f - eased * 2.0f, true);
        else              drawHalf (g, card, current, false, (eased - 0.5f) * 2.0f, true);
    }
    g.setColour (Colours::black);
    g.fillRect (card.getX(), card.getCentreY() - 1.5f, card.getWidth(), 3.0f);
    g.setColour (ff::pin);
    g.fillRoundedRectangle (outer.getX() + 1.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
    g.fillRoundedRectangle (outer.getRight() - 4.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
}

// =====================================================================================
// Pills and chips
// =====================================================================================
Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l, float size) : labels (std::move (l)), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { selected = (int) std::lround (v); repaint(); });
    attachment->sendInitialUpdate();
}

Rectangle<float> Pills::cell (int i) const
{
    const int n = labels.size();
    const float gap = 8.0f, w = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void Pills::paint (Graphics& g) { for (int i = 0; i < labels.size(); ++i) pill (g, cell (i).reduced (1.0f), labels[i], i == selected, textSize); }

void Pills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position)) { attachment->setValueAsCompleteGesture ((float) i); return; }
}

ToggleChip::ToggleChip (AudioProcessorValueTreeState& s, const String& paramId, const String& t, const String& tOn, float size)
    : text (t), onText (tOn.isEmpty() ? t : tOn), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void ToggleChip::paint (Graphics& g) { pill (g, getLocalBounds().toFloat().reduced (1.0f), on ? onText : text, on, textSize); }
void ToggleChip::mouseDown (const MouseEvent&) { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

MenuChip::MenuChip (AudioProcessorValueTreeState& s, const String& paramId, const String& p) : prefix (p)
{
    param = s.getParameter (paramId);
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v) { value = (int) std::lround (v); repaint(); });
    attachment->sendInitialUpdate();
}

void MenuChip::paint (Graphics& g)
{
    auto* c = dynamic_cast<AudioParameterChoice*> (param);
    const String name = c != nullptr ? c->choices[value] : param->getCurrentValueAsText();
    pill (g, getLocalBounds().toFloat().reduced (1.0f), (prefix + name).toUpperCase() + "  v", false, 12.0f);
}

void MenuChip::mouseDown (const MouseEvent&)
{
    auto* c = dynamic_cast<AudioParameterChoice*> (param);
    if (c == nullptr) return;
    PopupMenu m;
    for (int i = 0; i < c->choices.size(); ++i) m.addItem (i + 1, c->choices[i], true, i == value);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r > 0) attachment->setValueAsCompleteGesture ((float) (r - 1)); });
}

// =====================================================================================
// Knobs and sliders
// =====================================================================================
void FlipLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider&)
{
    const float cy = (float) y + (float) h * 0.5f, left = (float) x + 7.0f, right = (float) (x + w) - 7.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (left, cy - 3.0f, right - left, 6.0f, 3.0f);
    g.setColour (ff::isNeon() ? ff::tapeA : ff::ink);
    g.fillRoundedRectangle (left, cy - 3.0f, jmax (0.0f, pos - left), 6.0f, 3.0f);
    g.setColour (ff::ink);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void FlipLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    const float size = (float) jmin (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f - 1.0f;
    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);
    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        g.drawLine ({ { c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f }, { c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f } }, 2.0f);
    }
    Path hand;
    hand.addRoundedRectangle (-3.0f, -r * 0.68f, 6.0f, r * 0.83f, 2.0f);
    g.fillPath (hand, AffineTransform::rotation (start + pos * (end - start)).translated (c));
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
}

Dial::Dial (AudioProcessorValueTreeState& s, const String& paramId, const String& c, int size) : caption (c), knobSize (size)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

void Dial::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (18).withTrimmedBottom (22).withSizeKeepingCentre (knobSize, knobSize)); }

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, caption.toUpperCase(), r.removeFromTop (16), Justification::centred, ff::dim, 12.0f);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), r.removeFromBottom (18), Justification::centred, ff::ink, 14.0f);
}

LabelledSlider::LabelledSlider (AudioProcessorValueTreeState& s, const String& paramId, const String& c) : caption (c)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.onValueChange = [this] { repaint(); };
}

void LabelledSlider::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (16)); }

void LabelledSlider::paint (Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (16);
    ff::label (g, caption.toUpperCase(), top, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), top, Justification::centredRight, ff::ink, 11.0f);
}

// =====================================================================================
// The setting dial
// =====================================================================================
static constexpr float kDialSpan = 280.0f;
static Point<float> dialAt (Point<float> c, float deg, float r)
{
    const float a = degreesToRadians (deg);
    return { c.x + std::sin (a) * r, c.y - std::cos (a) * r };
}

void SettingDial::paint (Graphics& g)
{
    const auto& tab = db::tableFor (idxOf (proc, ParamIDs::model));
    const int n = tab.count, sel = jlimit (0, n - 1, idxOf (proc, ParamIDs::setting));
    const Point<float> c ((float) getWidth() * 0.5f, 140.0f);
    const float step = kDialSpan / (float) (n - 1);
    auto deg = [&] (int i) { return -kDialSpan * 0.5f + step * (float) i; };

    // the groups as arcs, the one in use lit
    int i0 = 0;
    while (i0 < n)
    {
        int i1 = i0;
        while (i1 + 1 < n && String (tab.s[(size_t) i1 + 1].group) == tab.s[(size_t) i0].group) ++i1;
        const bool lit = String (tab.s[(size_t) sel].group) == tab.s[(size_t) i0].group;
        const float a0 = deg (i0) - 4.0f, a1 = deg (i1) + 4.0f;
        Path arc;
        arc.addCentredArc (c.x, c.y, 116.0f, 116.0f, 0.0f, degreesToRadians (a0), degreesToRadians (a1), true);
        g.setColour (lit ? ff::ink : ff::line);
        g.strokePath (arc, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        const float mid = (a0 + a1) * 0.5f;
        const auto p = dialAt (c, mid, 132.0f);
        const auto just = mid < -50.0f ? Justification::centredRight : (mid > 50.0f ? Justification::centredLeft : Justification::centred);
        Rectangle<int> r ((int) p.x - 60, (int) p.y - 8, 120, 16);
        if (just == Justification::centredRight) r.setX ((int) p.x - 120);
        else if (just == Justification::centredLeft) r.setX ((int) p.x);
        ff::label (g, tab.s[(size_t) i0].group, r, just, lit ? ff::ink : ff::dim, 12.0f);
        i0 = i1 + 1;
    }
    g.setColour (ff::ring);
    g.fillEllipse (c.x - 106, c.y - 106, 212, 212);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 100, c.y - 100, 200, 200);
    for (int i = 0; i < n; ++i)
    {
        g.setColour (ff::ink);
        g.drawLine ({ dialAt (c, deg (i), 90.0f), dialAt (c, deg (i), 101.0f) }, i == sel ? 5.0f : 2.5f);
    }
    Path hand;
    hand.addRectangle (-1.6f, -82.0f, 3.2f, 100.0f);
    g.setColour (ff::red);
    g.fillPath (hand, AffineTransform::rotation (degreesToRadians (deg (sel))).translated (c));
    g.fillEllipse (c.x - 8, c.y - 8, 16, 16);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 3, c.y - 3, 6, 6);
    ff::label (g, "TAP A TICK OR PRESS C", { 0, (int) c.y + 40, getWidth(), 16 }, Justification::centred, ff::dim, 12.0f);
}

int SettingDial::hitTest (Point<float> pos) const
{
    const int n = db::tableFor (idxOf (proc, ParamIDs::model)).count;
    const Point<float> c ((float) getWidth() * 0.5f, 140.0f);
    const float a = radiansToDegrees (std::atan2 (pos.x - c.x, c.y - pos.y));
    const float step = kDialSpan / (float) (n - 1);
    return jlimit (0, n - 1, roundToInt ((a + kDialSpan * 0.5f) / step));
}

void SettingDial::mouseDown (const MouseEvent& e) { setParam (proc.apvts.getParameter (ParamIDs::setting), (float) hitTest (e.position)); }
void SettingDial::mouseDrag (const MouseEvent& e) { mouseDown (e); }

// =====================================================================================
// Treadle
// =====================================================================================
void TreadleView::drag (float delta) { setParam (proc.apvts.getParameter (ParamIDs::treadle), jlimit (0.0f, 1.0f, start + delta)); }
void TreadleView::mouseUp (const MouseEvent&) {}

void TreadleView::paint (Graphics& g)
{
    const float t = proc.getEngine().uiTreadle.load();
    const float w = (float) getWidth(), cx = w * 0.5f, cy = (float) getHeight() * 0.66f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (34.0f, cy - 2.0f, w - 68.0f, 26.0f, 8.0f);
    const float ang = degreesToRadians (-14.0f + 22.0f * t);
    Path rocker;
    rocker.addRoundedRectangle (44.0f - cx, -50.0f, w - 88.0f, 44.0f, 12.0f);
    g.setColour (ff::ink);
    g.fillPath (rocker, AffineTransform::rotation (ang).translated (cx, cy));
    g.setColour (ff::bg.withAlpha (0.55f));
    for (int i = 0; i < 9; ++i)
    {
        Path grip;
        grip.addRoundedRectangle (70.0f - cx + (float) i * 22.0f, -38.0f, 10.0f, 20.0f, 3.0f);
        g.fillPath (grip, AffineTransform::rotation (ang).translated (cx, cy));
    }
    g.setColour (ff::red);
    g.fillEllipse (cx - 8.0f, cy - 8.0f, 16.0f, 16.0f);
}

// =====================================================================================
// Ladders
// =====================================================================================
void PitchLadder::paint (Graphics& g)
{
    static const int ups[8] = { 2, 4, 5, 7, 9, 11, 12, 24 };
    static const char* names[8] = { "2", "3", "4", "5", "6", "7", "OCT", "2 OCT" };
    const float gap = 6.0f, w = ((float) getWidth() - gap * 7.0f) / 8.0f, h = ((float) getHeight() - gap) * 0.5f;
    const int a = std::abs (semis), sign = semis > 0 ? 1 : (semis < 0 ? -1 : 0);
    // which LEDs light: an exact interval lights one, a note in between lights the two around it
    int litA = -1, litB = -1;
    if (sign != 0)
    {
        for (int i = 0; i < 8; ++i) if (ups[i] == a) litA = i;
        if (litA < 0) for (int i = 1; i < 8; ++i) if (ups[i] > a && ups[i - 1] < a) { litA = i - 1; litB = i; }
        if (litA < 0 && a < 2) litA = 0;
    }
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < 8; ++i)
        {
            Rectangle<float> b ((float) i * (w + gap), (float) row * (h + gap), w, h);
            const bool on = sign != 0 && (row == 0) == (sign > 0) && (i == litA || i == litB);
            if (on) { g.setColour (ff::ink); g.fillRoundedRectangle (b, 6.0f); }
            else { g.setColour (ff::line); g.drawRoundedRectangle (b.reduced (1.0f), 6.0f, 2.0f); }
            ff::label (g, (row == 1 ? "-" : "") + String (names[i]), b.toNearestInt(), Justification::centred, on ? ff::bg : ff::dim, w < 40.0f ? 9.5f : 11.0f);
        }
}

void Trajectory::set (float frac, bool up, bool idle, double now)
{
    // idle, it cycles every 2 seconds in the chosen direction, as on the pedal
    float v = idle ? (float) std::fmod (now / 2.0, 1.0) : jlimit (0.0f, 1.0f, frac);
    if (std::abs (v - lit) > 0.01f || up != goingUp) { lit = v; goingUp = up; repaint(); }
}

void Trajectory::paint (Graphics& g)
{
    const int n = 12;
    const float gap = 4.0f, h = ((float) getHeight() - gap * (float) (n - 1)) / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const int fromBase = goingUp ? n - 1 - i : i;
        const bool on = fromBase < roundToInt (lit * (float) n);
        Rectangle<float> b (0.0f, (float) i * (h + gap), (float) getWidth(), h);
        if (on) { g.setColour (ff::isNeon() ? ff::tapeA : ff::teal); g.fillRoundedRectangle (b, 3.0f); }
        g.setColour (on && ff::isNeon() ? ff::ink : (on ? ff::teal : ff::line));
        g.drawRoundedRectangle (b.reduced (1.0f), 3.0f, 2.0f);
    }
}

// =====================================================================================
// Strip controls
// =====================================================================================
void FootButton::paint (Graphics& g)
{
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? ff::ink : ff::bg);
    g.fillRoundedRectangle (b, 14.0f);
    g.setColour (down ? ff::ink : ff::line);
    g.drawRoundedRectangle (b, 14.0f, 2.0f);
    const auto text = down ? ff::bg : ff::ink, sub = down ? ff::bg : ff::dim;
    ff::label (g, String::charToString ((juce_wchar) ('A' + sw)), { 12, 6, 40, 28 }, Justification::centredLeft, text, 24.0f);
    ff::label (g, job1, { 12, 36, getWidth() - 16, 18 }, Justification::centredLeft, sub, 12.0f);
    ff::label (g, job2, { 12, 54, getWidth() - 16, 18 }, Justification::centredLeft, sub, 12.0f);
}

void BigButton::set (const String& m, const String& s, Colour c)
{
    if (m == mainText && s == smallText && c == borderColour) return;
    mainText = m; smallText = s; borderColour = c;
    repaint();
}

void BigButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (pressed ? ff::ring : ff::bg);
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
    const float top = b.getCentreY() - 14.0f;
    ff::label (g, mainText, Rectangle<float> (b.getX(), top, b.getWidth(), 16.0f).toNearestInt(), Justification::centred, pressed ? ff::bg : ff::ink, 16.0f);
    ff::label (g, smallText, Rectangle<float> (b.getX(), top + 18.0f, b.getWidth(), 12.0f).toNearestInt(), Justification::centred, ff::dim, 10.5f);
}

void BigButton::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    pressed = true; repaint();
    if (onDown) onDown();
}

// =====================================================================================
// Pedal page
// =====================================================================================
FootswitchCard::FootswitchCard (DivebombProcessor& p, int s)
    : proc (p), sw (s), learn ("LEARN", [this] { proc.learnFootswitch (sw); }), reset ("DEFAULTS", [this] { proc.resetFootswitches(); })
{
    letter.setTextHeight (54.0f);
    letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
    addAndMakeVisible (letter);
    addAndMakeVisible (learn);
    addAndMakeVisible (reset);
}

void FootswitchCard::resized()
{
    letter.setBounds (16, 40, getWidth() - 32, 90);
    learn.setBounds (16, getHeight() - 48, (getWidth() - 40) / 2, 32);
    reset.setBounds (getWidth() / 2 + 4, getHeight() - 48, (getWidth() - 40) / 2, 32);
}

void FootswitchCard::paint (Graphics& g)
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    ff::label (g, "FOOTSWITCH", { 16, 14, 200, 18 });
    static const char* tapT[4] = { "ON / OFF", "MOMENTARY WHAMMY", "NEXT SETTING", "SHIFT LAYER" };
    static const char* holdT[4] = { "CLASSIC / CHORDS", "RISES WHILE HELD", "PREVIOUS SETTING", "MOMENTARY" };
    static const char* tapS[4] = { "ON / OFF", "PITCH 1 OR GLIDE", "PITCH 2", "THE DROP" };
    static const char* holdS[4] = { "TRILL ON / OFF", "WHILE HELD", "WHILE HELD", "WHILE HELD" };
    struct Row { const char* k; String v; };
    const Row rows[5] = { { "TREADLE TAP", tapT[sw] }, { "TREADLE HOLD", holdT[sw] }, { "STOMP TAP", tapS[sw] }, { "STOMP HOLD", holdS[sw] }, { "LISTENS FOR", sends } };
    int y = 142;
    for (const auto& r : rows)
    {
        g.setColour (ff::ring);
        g.fillRect (16, y, getWidth() - 32, 2);
        ff::label (g, r.k, { 16, y + 4, 110, 26 }, Justification::centredLeft, ff::dim, 12.0f);
        ff::label (g, r.v, { 110, y + 4, getWidth() - 126, 26 }, Justification::centredRight, ff::ink, 13.0f);
        y += 32;
    }
    static const char* combos[4] = { "A+B  SWITCH SECTION", "B+C  HOLD 1 S: NEXT MODEL", "C+D  NEXT DIVEBOMB, HOLD 1 S: KEEPER", "" };
    ff::label (g, combos[sw], { 16, y + 8, getWidth() - 32, 18 }, Justification::centredLeft, ff::dim, 11.0f);
}

void FootswitchCard::tick()
{
    const auto& s = proc.pedals();
    const String t = "CH " + String (s.fsChannel[(size_t) sw]) + String::fromUTF8 (" · CC ") + String (s.fsCC[(size_t) sw]);
    if (t != sends) { sends = t; repaint(); }
    learn.setLit (proc.learningWhat() == sw);
    learn.setText (proc.learningWhat() == sw ? "PRESS IT NOW" : "LEARN");
    letter.setAlpha (((proc.uiFootDown.load() >> sw) & 1) ? 0.6f : 1.0f);
}

void ExpressionMeter::paint (Graphics& g)
{
    String text = String::fromUTF8 ("EXPRESSION · ") + names::expTargets()[idxOf (proc, ParamIDs::expTarget)].toUpperCase() + "  v";
    if (proc.isCalibrating()) text = "ROCK HEEL TO TOE";
    else if (proc.learningWhat() == DivebombProcessor::LearnExpression) text = "MOVE THE PEDAL";
    ff::label (g, text, { 0, 0, getWidth(), 18 }, Justification::centredLeft, proc.isCalibrating() ? ff::ink : ff::dim, 12.0f);
    const float v = proc.expressionLevel();
    const float x0 = 10.0f, w = (float) getWidth() - 20.0f, y = 26.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (x0, y, w, 10.0f, 5.0f);
    g.setColour (nowSeconds() - proc.lastExpressionMove() < 1.5 ? ff::ink : ff::dim);
    g.fillRoundedRectangle (x0, y, w * v, 10.0f, 5.0f);
    g.fillEllipse (x0 + w * v - 9.0f, y - 4.0f, 18.0f, 18.0f);
}

void ExpressionMeter::mouseDown (const MouseEvent&)
{
    PopupMenu targets;
    const int target = idxOf (proc, ParamIDs::expTarget);
    for (int i = 0; i < names::expTargets().size(); ++i) targets.addItem (100 + i, names::expTargets()[i], true, i == target);
    PopupMenu curves;
    const char* const curveNames[3] = { "Linear", "Slow start", "Fast start" };
    for (int i = 0; i < 3; ++i) curves.addItem (200 + i, curveNames[i], true, proc.pedals().curve == i);
    PopupMenu map;
    map.addItem (300, "Off", true, proc.pedals().pedalMapChannel == 0);
    for (int ch = 1; ch <= 16; ++ch) map.addItem (300 + ch, "Channel " + String (ch), true, proc.pedals().pedalMapChannel == ch);
    PopupMenu m;
    m.addSubMenu ("Expression moves", targets);
    m.addItem (1, "Calibrate heel and toe");
    m.addItem (2, "Learn expression pedal");
    m.addSubMenu ("Curve", curves);
    m.addSeparator();
    m.addSubMenu ("Original pedal MIDI map", map);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1) proc.calibrateExpression();
        else if (r == 2) proc.learnExpression();
        else if (r >= 100 && r < 200) setParam (proc.apvts.getParameter (ParamIDs::expTarget), (float) (r - 100));
        else if (r >= 200 && r < 300) { proc.pedals().curve = r - 200; proc.pedals().save(); }
        else if (r >= 300) { proc.pedals().pedalMapChannel = r - 300; proc.pedals().save(); }
        repaint();
    });
}

void FootInputLabel::paint (Graphics& g)
{
    g.setColour (proc.pedalsDirect() ? ff::green : ff::ring);
    g.fillEllipse (0.0f, (float) getHeight() * 0.5f - 4.0f, 8.0f, 8.0f);
    ff::label (g, String::fromUTF8 ("FOOTSWITCHES · ") + (proc.pedalsDirect() ? "HEARD DIRECTLY" : "THROUGH ABLETON") + "  v",
               getLocalBounds().withTrimmedLeft (14), Justification::centredLeft, ff::dim, 12.0f);
}

void FootInputLabel::mouseDown (const MouseEvent&)
{
    PopupMenu m;
    m.addSectionHeader ("Footswitch input");
    m.addItem (-1, proc.footInputStatus(), false, false);
    const auto chosen = proc.footDevice();
    m.addItem (10, "Find my footswitch automatically", true, chosen.isEmpty());
    const auto devices = DirectMidiInput::deviceNames();
    for (int i = 0; i < devices.size() && i < 40; ++i) m.addItem (100 + i, "Use " + devices[i], true, chosen == devices[i]);
    m.addItem (11, "Only from a MIDI track in Ableton", true, chosen == "none");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, devices] (int r)
    {
        if (r == 10) proc.setFootDevice ({});
        else if (r == 11) proc.setFootDevice ("none");
        else if (r >= 100 && r - 100 < devices.size()) proc.setFootDevice (devices[r - 100]);
        repaint();
    });
}

// =====================================================================================
// Pebble
// =====================================================================================
void Pebble::update (double t, double dt)
{
    now = t;
    auto& eng = proc.getEngine();
    level = jmin (1.0f, eng.uiOutput.load() * 2.0f);
    asleep = rawOf (proc, ParamIDs::bypass) > 0.5f;

    // tall as the pitch rises, flat as it dives: follows what you hear, not just the treadle
    const float k = jlimit (-1.0f, 1.0f, eng.uiSemis.load() / 24.0f);
    stretch += (k - stretch) * (float) (1.0 - std::exp (-dt / 0.05));
    const int hits = eng.uiHits.load();
    if (hits != lastHits) { lastHits = hits; jump = 1.0f; }
    const int pulse = proc.uiFootPulse.load();
    if (pulse != lastPulse) { lastPulse = pulse; jump = jmax (jump, 0.5f); }
    jump *= (float) std::exp (-dt / 0.1);
    nodT *= (float) std::exp (-dt / 0.5);
    const float wobTarget = eng.uiDetune.load() ? 1.0f : 0.0f;
    wobble += (wobTarget - wobble) * (float) (1.0 - std::exp (-dt / 0.2));
    const bool expFresh = t - proc.lastExpressionMove() < 2.0;
    lean += ((expFresh ? (proc.expressionLevel() - 0.5f) * 12.0f : 0.0f) - lean) * (float) (1.0 - std::exp (-dt / 0.1));
    if (t > nextBlink) { blinkUntil = t + 0.13; nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 3.0; }
    blinking = t < blinkUntil;
    repaint();
}

void Pebble::paint (Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    const float s = a.getWidth() / 150.0f;
    const float sy = stretch >= 0.0f ? 1.0f + 0.45f * stretch : 1.0f + 0.4f * stretch;
    const float sx = stretch >= 0.0f ? 1.0f - 0.18f * stretch : 1.0f - 0.45f * stretch;
    const float tilt = degreesToRadians (lean + wobble * 7.0f * (float) std::sin (now * 19.0));
    const float lift = jump * 22.0f + (asleep ? 0.0f : 1.5f * (float) std::sin (now * 2.4)) - nodT * 8.0f * (float) std::sin (nodT * 12.0f);
    // squash and stretch from the feet, so Pebble stays standing
    const auto tf = AffineTransform::translation (-100.0f, -152.0f).scaled (s * sx, s * sy).rotated (tilt)
                        .translated (a.getCentreX(), a.getBottom() - 8.0f - lift * s);
    auto ell = [&] (float cx, float cy, float rx, float ry, Colour col) { Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2); g.setColour (col); g.fillPath (p, tf); };
    auto stroke = [&] (Path& p, float w, Colour col) { g.setColour (col); g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), tf); };

    if (level > 0.02f && ! asleep)
    {
        Path ring; ring.addEllipse (34, 48, 132, 112);
        stroke (ring, 5.0f, ff::teal.withAlpha (0.7f * level));
    }
    ell (107, 113, 58, 48, ff::blobShade);
    ell (100, 104, 58, 48, ff::blob);
    if (asleep || blinking)
        for (float ex : { 80.0f, 120.0f }) { Path lid; lid.startNewSubPath (ex - 12, 98); lid.quadraticTo (ex, 106, ex + 12, 98); stroke (lid, 6.0f, ff::faceInk); }
    else
        for (float ex : { 80.0f, 120.0f }) { ell (ex, 96, 14.0f, 14.0f, ff::eye); ell (ex + lean * 0.3f, 98, 7.0f, 7.0f, ff::faceInk); }
    if (asleep) { Path m; m.startNewSubPath (94, 128); m.lineTo (106, 128); stroke (m, 6.0f, ff::faceInk); }
    else if (level > 0.08f) ell (100, 128, 7.0f + 3.0f * level, 4.0f + 8.0f * level, ff::faceInk);
    else { Path m; m.startNewSubPath (90, 124); m.quadraticTo (100, 134, 110, 124); stroke (m, 6.0f, ff::faceInk); }

    if (ff::isNeon())
    {
        // a strip of gaffer tape across the head
        Path tape;
        tape.addRectangle (58.0f, 56.0f, 88.0f, 20.0f);
        const auto t2 = AffineTransform::rotation (degreesToRadians (-12.0f), 100.0f, 66.0f).followedBy (tf);
        g.setColour (ff::tapeB);
        g.fillPath (tape, t2);
        g.setColour (Colour (0xff101010));
        g.strokePath (tape, PathStrokeType (2.0f), t2);
    }
}

// =====================================================================================
// Neon skin: tape strips and the flip-card wipe
// =====================================================================================
void TapeLayer::paint (Graphics& g)
{
    if (! ff::isNeon()) return;
    struct Strip { float x, y, w, angle; const char* text; bool yellow; };
    static const Strip strips[] = {
        { 190, 62, 160, -5.0f, "LOW B, BABY", false },
        { 832, 492, 96, -24.0f, "", true },
        { 1222, 250, 120, 76.0f, "", false },
        { 930, 632, 120, -4.0f, "GO HIGHER", false },
        { 8, 512, 90, -14.0f, "", false } };
    for (const auto& s : strips)
    {
        Path p;
        // torn ends, as tape torn off by hand
        const float h = 30.0f;
        p.startNewSubPath (0, 3);
        p.lineTo (s.w * 0.03f, 0); p.lineTo (s.w * 0.97f, 0); p.lineTo (s.w, 3); p.lineTo (s.w * 0.98f, h * 0.5f);
        p.lineTo (s.w, h - 3); p.lineTo (s.w * 0.96f, h); p.lineTo (s.w * 0.04f, h); p.lineTo (0, h - 2); p.lineTo (s.w * 0.02f, h * 0.5f);
        p.closeSubPath();
        const auto tf = AffineTransform::rotation (degreesToRadians (s.angle), s.w * 0.5f, h * 0.5f).translated (s.x, s.y);
        g.setColour (s.yellow ? ff::tapeB : ff::tapeA);
        g.fillPath (p, tf);
        g.setColour (Colours::black.withAlpha (0.06f));
        for (float x = 4.0f; x < s.w; x += 6.0f) { Path fibre; fibre.addRectangle (x, 2.0f, 1.5f, h - 4.0f); g.fillPath (fibre, tf); }
        if (s.yellow) { g.setColour (Colour (0xff101010).withAlpha (0.5f)); g.strokePath (p, PathStrokeType (1.0f), tf); }
        if (*s.text)
        {
            Graphics::ScopedSaveState save (g);
            g.addTransform (tf);
            g.setColour (Colour (0xff101010));
            g.setFont (ff::marker (17.0f));
            g.drawText (s.text, Rectangle<float> (0, 0, s.w, h), Justification::centred, false);
        }
    }
}

void Wipe::start (bool toNeon)
{
    neon = toNeon; t = 0.0f; swapped = false;
    setVisible (true);
    repaint();
}

void Wipe::advance (double dt)
{
    if (t < 0.0f) return;
    t += (float) dt;
    const float covered = 13 * 0.034f + 5 * 0.012f + 0.32f;
    if (! swapped && t >= covered) { swapped = true; if (onSwap) onSwap(); }
    if (t > covered + 0.22f) { t = -1.0f; setVisible (false); }
    repaint();
}

void Wipe::paint (Graphics& g)
{
    if (t < 0.0f) return;
    const int cols = 14, rows = 6;
    const float gap = 3.0f, w = ((float) getWidth() - gap * (cols - 1)) / cols, h = ((float) getHeight() - gap * (rows - 1)) / rows;
    const float covered = 13 * 0.034f + 5 * 0.012f + 0.32f;
    const float fade = swapped ? jmax (0.0f, 1.0f - (t - covered) / 0.22f) : 1.0f;
    const Colour to (neon ? 0xffe9ff12 : 0xff050505);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            // each tile turns over on its hinge, left to right, like a row of flip cards
            const float local = jlimit (0.0f, 1.0f, (t - (float) c * 0.034f - (float) r * 0.012f) / 0.32f);
            const float eased = 1.0f - std::pow (1.0f - local, 3.0f);
            if (eased <= 0.0f) continue;
            const float x = (float) c * (w + gap), y = (float) r * (h + gap), th = h * eased;
            g.setColour (to.withAlpha (fade));
            g.fillRoundedRectangle (x, y + (h - th) * 0.5f, w, th, 4.0f);
            g.setColour (Colours::black.withAlpha (0.35f * fade));
            g.fillRect (x, y + h * 0.5f - 1.0f, w, 2.0f);
        }
}
} // namespace ui

// =====================================================================================
// Editor
// =====================================================================================
using namespace ui;
using namespace ParamIDs;


DivebombEditor::DivebombEditor (DivebombProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      models (p.apvts, model, { "FIVE", "DUAL", "NEON", "BASS" }),
      sections (p.apvts, section, { "TREADLE", "STOMP" }),
      latency (p.apvts, ParamIDs::latency, { "LIVE", "STUDIO" }),
      viewMain ("MAIN", [this] { setView (View::Main); }),
      viewDeep ("DEEP", [this] { setView (View::Deep); }),
      viewPedal ("PEDAL", [this] { setView (View::Pedal); }),
      settingDial (p),
      algo (p.apvts, ParamIDs::algo, { "CLASSIC", "CHORDS" }),
      layerType (p.apvts, ParamIDs::layerType, { "DROP", "DETUNE" }),
      anchor (p.apvts, ParamIDs::anchor, { "AUTO", "ON", "OFF" }),
      input (p.apvts, ParamIDs::input, { "GUITAR", "BASS" }),
      treadleView (p),
      riseSlider (p.apvts, rise, "Rise on B"), fallSlider (p.apvts, fall, "Fall"),
      trails (p.apvts, ParamIDs::trails, "NO TRAILS", "TRAILS"),
      layerOn (p.apvts, ParamIDs::layerOn, "LAYER OFF", "LAYER ON"),
      layerDown ("DOWN", [this] { proc.stepChoice (layerStep, 1); }),
      layerUp ("UP", [this] { proc.stepChoice (layerStep, -1); }),
      layerGlide (p.apvts, ParamIDs::layerGlide, "Glide"), glitchDial (p.apvts, glitch, "Glitch"), crossover (p.apvts, anchorHz, "Crossover"),
      dropLower ("LOWER", [this] { proc.stepChoice (dropStep, 1); }),
      dropHigher ("HIGHER", [this] { proc.stepChoice (dropStep, -1); }),
      dropMode (p.apvts, dropLatch, { "MOMENTARY", "LATCH" }),
      jump (p.apvts, ParamIDs::jump, { "HAMMER", "GLIDE" }),
      retrigger (p.apvts, ParamIDs::retrigger, { "ONCE", "AGAIN", "LOOP" }, 10.0f),
      algoStomp (p.apvts, ParamIDs::algoStomp, { "CLASSIC", "CHORDS" }),
      glideDir (p.apvts, glideDown, { "UP", "DOWN" }),
      glideMode (p.apvts, glideLatch, { "MOMENTARY", "LATCH" }),
      anchor2 (p.apvts, ParamIDs::anchor, { "AUTO", "ON", "OFF" }),
      modeChip ("HAMMER", [this] { proc.stepChoice (hamMode, 1, true); }),
      p1Down ("P1 -", [this] { proc.stepChoice (p1, -1); }), p1Up ("P1 +", [this] { proc.stepChoice (p1, 1); }),
      p2Down ("P2 -", [this] { proc.editPitch2 (-1); }), p2Up ("P2 +", [this] { proc.editPitch2 (1); }),
      p2bDown ("2ND -", [this] { proc.stepChoice (p2b, -1); }), p2bUp ("2ND +", [this] { proc.stepChoice (p2b, 1); }),
      glideLess ("SMALLER", [this] { proc.stepChoice (glideStep, -1); }), glideMore ("BIGGER", [this] { proc.stepChoice (glideStep, 1); }),
      tapChip ("TAP", [this] { proc.tapTrill(); }),
      dropOn (p.apvts, ParamIDs::dropOn, "LATCHED OFF", "LATCHED ON"),
      dryPlus (p.apvts, ParamIDs::dryPlus, "DRY +"), trill (p.apvts, ParamIDs::trill, "TRILL"),
      trillSync (p.apvts, ParamIDs::trillSync, "FREE", "SYNC"),
      bandwidth (p.apvts, ParamIDs::bandwidth, "FULL RANGE", "ORIGINAL TONE"),
      trillDiv (p.apvts, ParamIDs::trillDiv),
      glideShift (p.apvts, ParamIDs::glideShift, "Shift"), glideReturn (p.apvts, ParamIDs::glideReturn, "Return"),
      trillHz (p.apvts, ParamIDs::trillHz, "Rate"),
      level (p.apvts, ParamIDs::level, "Level"), deepGlitch (p.apvts, glitch, "Glitch"),
      deepRise (p.apvts, rise, "B rise"), deepFall (p.apvts, fall, "B fall"),
      deepLayerGlide (p.apvts, ParamIDs::layerGlide, "Layer glide"), deepAnchorHz (p.apvts, anchorHz, "Low Anchor"),
      deepTrails (p.apvts, ParamIDs::trails, "BYPASS CUTS", "BYPASS SLIDES HOME"),
      deepBandwidth (p.apvts, ParamIDs::bandwidth, "STOMP CHORDS FULL RANGE", "STOMP CHORDS ORIGINAL TONE"),
      expTarget (p.apvts, ParamIDs::expTarget, "Expression: "),
      expression (p), footInput (p), pebble (p)
{
    setLookAndFeel (&lnf);
    ff::setNeon (proc.apvts.getRawParameterValue (model)->load() > 1.5f && proc.apvts.getRawParameterValue (model)->load() < 2.5f);

    for (auto* c : { &groupCard, &settingCard, &pitchCard, &layerCard, &dropCard, &bCard, &cCard, &rateCard, &latencyCard })
        addAndMakeVisible (c);
    groupCard.setTextHeight (26.0f); settingCard.setTextHeight (24.0f); pitchCard.setTextHeight (40.0f);
    layerCard.setTextHeight (30.0f); dropCard.setTextHeight (48.0f); bCard.setTextHeight (24.0f); cCard.setTextHeight (24.0f);
    rateCard.setTextHeight (28.0f); latencyCard.setTextHeight (18.0f);

    for (auto* c : std::initializer_list<Component*> { &models, &sections, &latency, &viewMain, &viewDeep, &viewPedal,
                                                       &settingDial, &algo, &layerType, &anchor, &input, &treadleView, &riseSlider, &fallSlider,
                                                       &trails, &layerOn, &layerDown, &layerUp, &layerGlide, &glitchDial, &crossover,
                                                       &dropLower, &dropHigher, &dropMode, &jump, &retrigger, &algoStomp, &glideDir, &glideMode, &anchor2,
                                                       &modeChip, &p1Down, &p1Up, &p2Down, &p2Up, &p2bDown, &p2bUp, &glideLess, &glideMore, &tapChip,
                                                       &dropOn, &dryPlus, &trill, &trillSync, &bandwidth, &trillDiv, &glideShift, &glideReturn, &trillHz,
                                                       &ladder, &trajectory, &level, &deepGlitch, &deepRise, &deepFall, &deepLayerGlide, &deepAnchorHz,
                                                       &deepTrails, &deepBandwidth, &expTarget, &expression, &footInput, &pebble, &keepButton, &bypassButton })
        addAndMakeVisible (c);
    for (int i = 0; i < 4; ++i)
    {
        foot[(size_t) i] = std::make_unique<FootButton> (p, i);
        addAndMakeVisible (*foot[(size_t) i]);
        footCards[(size_t) i] = std::make_unique<FootswitchCard> (p, i);
        addChildComponent (*footCards[(size_t) i]);
    }

    treadleOnly = { &groupCard, &settingCard, &settingDial, &algo, &pitchCard, &treadleView, &riseSlider, &fallSlider, &trails,
                    &layerCard, &layerDown, &layerUp, &layerType, &layerOn, &layerGlide, &glitchDial, &crossover, &anchor, &input };
    stompOnly = { &dropCard, &dropLower, &dropHigher, &dropMode, &dropOn, &jump, &bCard, &cCard, &modeChip, &retrigger, &p1Down, &p1Up,
                  &p2Down, &p2Up, &p2bDown, &p2bUp, &glideLess, &glideMore, &glideDir, &glideMode, &glideShift, &glideReturn,
                  &ladder, &dryPlus, &trill, &rateCard, &trillSync, &tapChip, &trillDiv, &trillHz, &trajectory, &algoStomp, &bandwidth, &anchor2 };
    deepOnly = { &level, &deepGlitch, &deepRise, &deepFall, &deepLayerGlide, &deepAnchorHz, &deepTrails, &deepBandwidth, &expTarget };
    pedalOnly = { &expression, &footInput };

    keepButton.onDown = [this] { proc.keepNow(); };
    keepButton.onRightClick = [this] { keeperMenu(); };
    bypassButton.onDown = [this] { setParam (proc.apvts.getParameter (bypass), raw (bypass) > 0.5f ? 0.0f : 1.0f); };

    noteEditor.setTextToShowWhenEmpty ("Add a note, Enter saves", ff::dim);
    noteEditor.setFont (ff::font (13.0f));
    noteEditor.onReturnKey = [this] { closeNote (true); };
    noteEditor.onEscapeKey = [this] { closeNote (false); };
    addChildComponent (noteEditor);

    addAndMakeVisible (tape);
    addChildComponent (wipe);
    wipe.onSwap = [this]
    {
        ff::setNeon (idx (model) == db::ModelNeon);
        noteEditor.applyColourToAllText (ff::ink);
        repaint();
        for (auto* c : getChildren()) c->repaint();
    };

    setSize (1260, 660);
    setResizable (true, true);
    setResizeLimits (630, 330, 2520, 1320);
    getConstrainer()->setFixedAspectRatio (1260.0 / 660.0);
    setView (View::Main);
    startTimerHz (60);
}

DivebombEditor::~DivebombEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DivebombEditor::setView (View v)
{
    view = v;
    viewMain.setLit (v == View::Main); viewDeep.setLit (v == View::Deep); viewPedal.setLit (v == View::Pedal);
    layoutSections();
    repaint();
}

void DivebombEditor::layoutSections()
{
    const bool main = view == View::Main;
    const bool stomp = idx (section) == 1;
    for (auto* c : treadleOnly) c->setVisible (main && ! stomp);
    for (auto* c : stompOnly) c->setVisible (main && stomp);
    for (auto* c : deepOnly) c->setVisible (view == View::Deep);
    for (auto* c : pedalOnly) c->setVisible (view == View::Pedal);
    for (auto& c : footCards) c->setVisible (view == View::Pedal);
    if (main && stomp)
    {
        // footswitch B is the hammer's Pitch 1 or the glide, C is always Pitch 2
        const bool glide = idx (ParamIDs::jump) == 1;
        for (auto* c : { (Component*) &p1Down, (Component*) &p1Up }) c->setVisible (! glide);
        for (auto* c : { (Component*) &glideLess, (Component*) &glideMore, (Component*) &glideDir, (Component*) &glideMode, (Component*) &glideShift, (Component*) &glideReturn })
            c->setVisible (glide);
        const int mode = idx (hamMode);
        retrigger.setVisible (db::seqLength (mode) > 0);
        p2bDown.setVisible (mode == db::HamImpossible);
        p2bUp.setVisible (mode == db::HamImpossible);
        if (glide) { ladder.setBounds (658, 330, 256, 64); dryPlus.setBounds (658, 410, 124, 34); trill.setBounds (790, 410, 124, 34); }
        else { ladder.setBounds (386, 312, 528, 64); dryPlus.setBounds (386, 392, 256, 34); trill.setBounds (658, 392, 256, 34); }
        trillHz.setVisible (raw (ParamIDs::trillSync) < 0.5f);
        trillDiv.setVisible (raw (ParamIDs::trillSync) > 0.5f);
    }
    lastSection = idx (section);
}

void DivebombEditor::resized()
{
    const float s = (float) getWidth() / 1260.0f;
    for (auto* c : getChildren()) c->setTransform (AffineTransform::scale (s));

    // header
    models.setBounds (226, 24, 268, 32);
    sections.setBounds (588, 24, 186, 32);
    latencyCard.setBounds (868, 20, 84, 40);
    latency.setBounds (960, 24, 110, 32);
    viewMain.setBounds (1084, 24, 50, 32); viewDeep.setBounds (1138, 24, 50, 32); viewPedal.setBounds (1192, 24, 52, 32);

    // treadle section
    groupCard.setBounds (36, 120, 150, 72);
    settingCard.setBounds (194, 120, 240, 72);
    settingDial.setBounds (36, 200, 398, 250);
    algo.setBounds (36, 464, 240, 34);
    pitchCard.setBounds (486, 120, 328, 84);
    treadleView.setBounds (486, 214, 328, 150);
    riseSlider.setBounds (486, 400, 156, 34);
    fallSlider.setBounds (658, 400, 156, 34);
    trails.setBounds (674, 456, 140, 34);
    layerCard.setBounds (866, 120, 150, 72);
    layerDown.setBounds (1026, 126, 95, 30); layerUp.setBounds (1129, 126, 95, 30);
    layerType.setBounds (1026, 162, 198, 30);
    layerOn.setBounds (866, 204, 150, 32);
    layerGlide.setBounds (866, 250, 110, 118); glitchDial.setBounds (990, 250, 110, 118); crossover.setBounds (1114, 250, 110, 118);
    anchor.setBounds (866, 412, 170, 34);
    input.setBounds (1050, 412, 174, 34);

    // stomp section
    dropCard.setBounds (36, 120, 298, 96);
    dropLower.setBounds (36, 228, 145, 32); dropHigher.setBounds (189, 228, 145, 32);
    dropMode.setBounds (36, 300, 298, 34);
    dropOn.setBounds (36, 346, 298, 32);
    jump.setBounds (386, 118, 256, 34);
    bCard.setBounds (386, 166, 256, 72);
    p1Down.setBounds (386, 250, 124, 32); p1Up.setBounds (518, 250, 124, 32);
    glideLess.setBounds (386, 250, 124, 32); glideMore.setBounds (518, 250, 124, 32);
    glideDir.setBounds (386, 290, 256, 30);
    glideMode.setBounds (386, 326, 256, 30);
    glideShift.setBounds (396, 366, 110, 118); glideReturn.setBounds (522, 366, 110, 118);
    modeChip.setBounds (658, 118, 112, 34);
    retrigger.setBounds (776, 118, 138, 34);
    cCard.setBounds (658, 166, 256, 72);
    p2Down.setBounds (658, 250, 124, 32); p2Up.setBounds (790, 250, 124, 32);
    p2bDown.setBounds (658, 288, 124, 30); p2bUp.setBounds (790, 288, 124, 30);
    ladder.setBounds (658, 330, 256, 64);
    dryPlus.setBounds (658, 410, 124, 34); trill.setBounds (790, 410, 124, 34);
    rateCard.setBounds (966, 120, 150, 72);
    trillSync.setBounds (1126, 126, 98, 28); tapChip.setBounds (1126, 162, 98, 28);
    trillDiv.setBounds (966, 204, 150, 30);
    trillHz.setBounds (966, 240, 110, 118);
    trajectory.setBounds (1150, 214, 40, 150);
    algoStomp.setBounds (966, 380, 258, 30);
    bandwidth.setBounds (966, 418, 258, 30);
    anchor2.setBounds (966, 462, 258, 34);

    // deep page
    const int dx[6] = { 40, 230, 420, 610, 800, 990 };
    Dial* dials[6] = { &level, &deepGlitch, &deepRise, &deepFall, &deepLayerGlide, &deepAnchorHz };
    for (int i = 0; i < 6; ++i) dials[i]->setBounds (dx[i], 120, 160, 150);
    deepTrails.setBounds (40, 320, 360, 34);
    deepBandwidth.setBounds (420, 320, 360, 34);
    expTarget.setBounds (800, 320, 400, 34);

    // pedal page
    for (int i = 0; i < 4; ++i) footCards[(size_t) i]->setBounds (20 + i * 305, 76, 290, 396);
    footInput.setBounds (24, 484, 380, 26);
    expression.setBounds (430, 480, 500, 40);

    // strip
    pebble.setBounds (30, 516, 110, 124);
    for (int i = 0; i < 4; ++i) foot[(size_t) i]->setBounds (322 + i * 142, 546, 132, 84);
    keepButton.setBounds (1046, 552, 84, 72);
    bypassButton.setBounds (1138, 552, 90, 72);
    noteEditor.setBounds (150, 604, 160, 26);
    tape.setBounds (0, 0, 1260, 660);
    wipe.setBounds (0, 0, 1260, 660);
}

void DivebombEditor::paint (Graphics& g)
{
    g.fillAll (ff::bg);
    g.addTransform (AffineTransform::scale ((float) getWidth() / 1260.0f));
    ff::label (g, "DIVEBOMB", { 20, 20, 140, 40 }, Justification::centredLeft, ff::ink, 26.0f);
    ff::label (g, "MODEL", { 170, 20, 56, 40 });
    ff::label (g, "SECTION", { 514, 20, 72, 40 });
    ff::label (g, "LATENCY", { 794, 20, 72, 40 });

    auto face = [&] (int x, int y, int w, int h) { g.setColour (ff::face); g.fillRoundedRectangle ((float) x, (float) y, (float) w, (float) h, 12.0f); };
    const bool stomp = idx (section) == 1;
    if (view == View::Main && ! stomp)
    {
        face (20, 76, 430, 440); face (470, 76, 360, 440); face (850, 76, 390, 440);
        ff::label (g, "SETTING", { 36, 88, 200, 20 });
        ff::label (g, "TREADLE", { 486, 88, 200, 20 });
        ff::label (g, idx (model) == db::ModelDual ? "DROP TUNE" : "SHIFT LAYER", { 866, 88, 200, 20 });
        ff::label (g, "ON D", { 1100, 88, 124, 20 }, Justification::centredRight);
        const auto& tab = db::tableFor (idx (model));
        ff::label (g, String (jlimit (0, tab.count - 1, idx (setting)) + 1) + " OF " + String (tab.count), { 290, 464, 144, 34 }, Justification::centredRight);
        const auto& st = tab.s[(size_t) jlimit (0, tab.count - 1, idx (setting))];
        const String toe = st.kind == db::Kind::Harmony || st.kind == db::Kind::Octave ? String (st.name).fromFirstOccurrenceOf ("/ ", false, false)
                         : (st.kind == db::Kind::Detune ? String ("FULL DETUNE") : String (st.name));
        ff::label (g, "HEEL " + String::fromUTF8 ("· ") + (st.kind == db::Kind::Harmony || st.kind == db::Kind::Octave ? String (st.name).upToFirstOccurrenceOf (" /", false, false) : String ("DRY")), { 486, 368, 160, 20 });
        ff::label (g, "TOE " + String::fromUTF8 ("· ") + toe, { 654, 368, 160, 20 }, Justification::centredRight);
        ff::label (g, "B: MOMENTARY WHAMMY", { 486, 458, 180, 30 });
        const int ls = idx (layerStep);
        const auto& L = db::layerTable()[(size_t) jlimit (0, db::kLayerSteps - 1, ls)];
        String desc = idx (ParamIDs::layerType) == 1 ? String ("A DETUNED COPY UNDER THE BEND")
                    : String (L.semis < 0 ? "DOWN " : "UP ") + (std::abs (L.semis) == 12.0f ? String ("AN OCTAVE") : String ((int) std::abs (L.semis)) + (std::abs (L.semis) > 1 ? " SEMITONES" : " SEMITONE")) + (L.dry ? " + DRY" : "");
        ff::label (g, desc, { 1026, 204, 198, 32 }, Justification::centredLeft, ff::dim, 11.0f);
        ff::label (g, "LOW END", { 866, 390, 160, 20 });
        ff::label (g, "INPUT", { 1050, 390, 160, 20 });
        const bool anchorOn = idx (ParamIDs::anchor) == 1 || (idx (ParamIDs::anchor) == 0 && (idx (ParamIDs::input) == 1 || idx (model) == db::ModelNeon));
        ff::label (g, anchorOn ? "DRY BELOW " + String (roundToInt (raw (anchorHz))) + " HZ" : String ("LOW ANCHOR OFF"), { 866, 456, 358, 30 });
    }
    else if (view == View::Main)
    {
        face (20, 76, 330, 440); face (370, 76, 560, 440); face (950, 76, 290, 440);
        ff::label (g, String::fromUTF8 ("DROP · ON D"), { 36, 88, 200, 20 });
        ff::label (g, String::fromUTF8 ("B · ") + (idx (ParamIDs::jump) == 1 ? "GLIDE" : "PITCH 1"), { 386, 88, 200, 20 });
        ff::label (g, String::fromUTF8 ("C · PITCH 2"), { 658, 88, 200, 20 });
        ff::label (g, "TRILL + TRAJECTORY", { 966, 88, 200, 20 });
        const auto& D = db::dropTable()[(size_t) jlimit (0, db::kDropSteps - 1, idx (dropStep))];
        ff::label (g, D.dry ? String ("DOWN AN OCTAVE + DRY") : (D.semis == -12.0f ? String ("DOWN AN OCTAVE") : "DOWN " + String ((int) -D.semis) + (D.semis < -1 ? " SEMITONES" : " SEMITONE")),
                   { 36, 268, 298, 24 });
        g.setColour (ff::dim);
        g.setFont (ff::font (11.0f, 0.06f));
        g.drawFittedText ("LATCH IT ON, THEN SWITCH TO MOMENTARY, AND D BECOMES A KILL SWITCH, AS ON THE ORIGINAL", Rectangle<int> (36, 392, 298, 50), Justification::topLeft, 3);
        static const char* modeText[5] = { "C IS A SECOND HAMMER-ON", "EACH PRESS ALTERNATES TWO PITCHES", "C STEPS THROUGH 2 TRANSPOSITIONS", "C STEPS THROUGH 3 TRANSPOSITIONS", "C STEPS THROUGH 5 TRANSPOSITIONS" };
        ff::label (g, modeText[jlimit (0, 4, idx (hamMode))], { 658, 452, 256, 30 }, Justification::centredLeft, ff::dim, 11.0f);
        if (idx (ParamIDs::jump) == 0) ff::label (g, "B JUMPS WHILE HELD, BACK ON RELEASE", { 386, 452, 256, 30 }, Justification::centredLeft, ff::dim, 11.0f);
        ff::label (g, "EXPRESSION SETS THE RATE", { 966, 358, 180, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    }
    else if (view == View::Deep)
    {
        face (20, 76, 1220, 440);
        ff::label (g, "DEEP", { 36, 88, 200, 20 });
        g.setColour (ff::ink);
        g.setFont (ff::font (14.0f, 0.04f));
        const bool studio = idx (ParamIDs::latency) == 1;
        g.drawFittedText (String ("LATENCY: ") + (studio ? "STUDIO. THE CLEANEST CHORDS, A FIXED " + String (proc.reportedLatency() * 1000.0 / jmax (1.0, proc.getSampleRate()), 1) + " MS DELAY THAT ABLETON LINES UP. FOR RECORDING."
                                                          : "LIVE. THE SHIFTED NOTE ARRIVES ABOUT 2 TO 5 MS AFTER A GUITAR NOTE AND 9 TO 12 MS AFTER A LOW BASS NOTE, AS A CLEAN SHIFT NEEDS ABOUT ONE CYCLE OF THE NOTE. LOW ANCHOR KEEPS YOUR LOW END DRY AND INSTANT UNDERNEATH."),
                          Rectangle<int> (40, 380, 1180, 60), Justification::topLeft, 3);
        ff::label (g, "GLITCH 1 IS THE ORIGINAL'S TRACKING. 0 IS CLEAN, 2 IS WILDER.", { 40, 450, 1180, 20 }, Justification::centredLeft, ff::dim, 12.0f);
    }
    else
    {
        ff::label (g, "SET UP IN ABLETON: TURN TRACK, SYNC AND REMOTE OFF FOR YOUR FOOTSWITCH IN PREFERENCES, LINK, MIDI. DIVEBOMB READS IT DIRECTLY.", { 24, 514, 1200, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    }

    // the strip
    face (20, 532, 1220, 112);
    ff::label (g, "PEBBLE", { 150, 548, 160, 16 });
    ff::label (g, mood, { 150, 566, 170, 30 }, Justification::centredLeft, ff::ink, 22.0f);
    if (! noteEditor.isVisible()) ff::label (g, moodSub, { 150, 598, 170, 18 });
    ff::label (g, "A+B SECTION", { 892, 550, 150, 18 });
    ff::label (g, "B+C HOLD MODEL", { 892, 572, 150, 18 });
    ff::label (g, "C+D HOLD KEEPER", { 892, 594, 150, 18 });
}

void DivebombEditor::paintOverChildren (Graphics&) {}

void DivebombEditor::keeperMenu()
{
    PopupMenu m;
    m.addItem (1, "Add a note to Keeper No. " + String (proc.lastKeeper()), proc.lastKeeper() > 0);
    m.addItem (2, "Export for Claude");
    m.addItem (3, "Open the Keepers folder");
    m.addSeparator();
    m.addItem (5, "Live", true, proc.keeperLive());
    m.addItem (6, "Studio", true, ! proc.keeperLive());
    m.addItem (4, "Learn a Keeper footswitch");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&keepButton), [this] (int r)
    {
        if (r == 1)
        {
            noteNumber = proc.lastKeeper();
            noteEditor.clear();
            noteEditor.setVisible (true);
            noteEditor.grabKeyboardFocus();
            noteUntil = nowSeconds() + 20.0;
        }
        else if (r == 2) proc.exportKeepers();
        else if (r == 3) proc.openKeepersFolder();
        else if (r == 4) proc.learnKeeper();
        else if (r == 5 || r == 6) proc.setKeeperLive (r == 5);
    });
}

void DivebombEditor::closeNote (bool save)
{
    if (save) proc.addKeeperNote (noteNumber, noteEditor.getText());
    if (save && noteEditor.getText().trim().isNotEmpty()) proc.say ("NOTED");
    noteEditor.setVisible (false);
    noteEditor.clear();
    noteUntil = 0.0;
    repaint();
}

static String noteNameOf (float hz)
{
    if (hz <= 0.0f) return "WAITING FOR A NOTE";
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
    return String ("TRACKING ") + n[(midi % 12 + 12) % 12] + String (midi / 12 - 1);
}

void DivebombEditor::timerCallback()
{
    const double now = nowSeconds();
    const double dt = jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    auto& eng = proc.getEngine();

    // the NEON model repaints the panel with a flip-card wipe; any other model flips back
    const int m = idx (model);
    if (m != lastModel)
    {
        const bool wantNeon = m == db::ModelNeon;
        if (lastModel >= 0 && wantNeon != ff::isNeon()) wipe.start (wantNeon);
        lastModel = m;
        repaint();
    }
    wipe.advance (dt);
    if (idx (section) != lastSection) { layoutSections(); repaint(); }
    static int lastLayoutKey = -1;
    const int layoutKey = idx (ParamIDs::jump) + 2 * idx (hamMode) + 20 * (raw (ParamIDs::trillSync) > 0.5f ? 1 : 0);
    if (layoutKey != lastLayoutKey) { lastLayoutKey = layoutKey; layoutSections(); repaint(); }

    for (auto* c : { &groupCard, &settingCard, &pitchCard, &layerCard, &dropCard, &bCard, &cCard, &rateCard, &latencyCard }) c->advance (dt);

    // treadle section
    const auto& tab = db::tableFor (m);
    const auto& st = tab.s[(size_t) jlimit (0, tab.count - 1, idx (setting))];
    groupCard.setValue (st.group);
    settingCard.setValue (st.name);
    const float semis = eng.uiSemis.load();
    pitchCard.setValue ((semis >= -0.05f ? "+" : "-") + String (std::abs (semis), 1) + " ST");
    const auto& L = db::layerTable()[(size_t) jlimit (0, db::kLayerSteps - 1, idx (layerStep))];
    layerCard.setValue (idx (ParamIDs::layerType) == 1 ? String ("DETUNE") : String (L.name));
    layerCard.setAlpha (eng.uiLayerActive.load() ? 1.0f : 0.7f);
    settingDial.repaint();
    treadleView.repaint();
    latencyCard.setValue (String (eng.uiDelayMs.load(), 1) + " MS");

    // stomp section
    const auto& D = db::dropTable()[(size_t) jlimit (0, db::kDropSteps - 1, idx (dropStep))];
    dropCard.setValue (D.name);
    dropCard.setAlpha (eng.uiDropActive.load() ? 1.0f : 0.7f);
    const auto& G = db::glideTable()[(size_t) jlimit (0, db::kGlideSteps - 1, idx (glideStep))];
    const int mode = idx (hamMode);
    if (idx (ParamIDs::jump) == 1) bCard.setValue (String ("GLIDE ") + G.name + (idx (glideDown) == 1 ? " DN" : " UP"));
    else bCard.setValue ("P1 " + names::pitchName (db::hammerSemis (idx (p1))));
    const int len = db::seqLength (mode);
    if (len > 0)
    {
        const int stp = jlimit (0, len - 1, proc.sequenceStep());
        const char* id = len == 2 ? seq2[stp] : (len == 3 ? seq3[stp] : seq5[stp]);
        cCard.setValue ("STEP " + String (stp + 1) + " " + names::pitchName (db::hammerSemis (idx (id))));
    }
    else if (mode == db::HamImpossible)
        cCard.setValue (names::pitchName (db::hammerSemis (idx (p2)))
                        + String::fromUTF8 (" / ") + names::pitchName (db::hammerSemis (idx (p2b))));
    else cCard.setValue ("P2 " + names::pitchName (db::hammerSemis (idx (p2))));
    static const Colour* modeCols[5] = { &ff::purple, &ff::red, &ff::blue, &ff::teal, &ff::green };
    modeChip.setText (names::hamModes()[jlimit (0, 4, mode)]);
    modeChip.setLit (true, mode == 3 ? Colour (0xff3fd6c8) : *modeCols[jlimit (0, 4, mode)]);
    ladder.setSemis (roundToInt (semis - (eng.uiDropActive.load() ? D.semis : 0.0f)));
    static const char* divNames[5] = { "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    rateCard.setValue (raw (ParamIDs::trillSync) > 0.5f ? String (divNames[jlimit (0, 4, idx (ParamIDs::trillDiv))]) : String (raw (ParamIDs::trillHz), 1) + " HZ");
    const float gv = eng.uiGlide.load();
    trajectory.set (std::abs (gv) / jmax (1.0f, G.semis), idx (glideDown) == 0, std::abs (gv) < 0.01f && ! eng.uiGlideActive.load(), now);

    // the strip
    const bool stomp = idx (section) == 1, glide = idx (ParamIDs::jump) == 1;
    static const char* modeShort[5] = { "HAMMER", "IMPOSSIBLE", "SEQUENCE", "SEQUENCE", "SEQUENCE" };
    if (! stomp)
    {
        foot[0]->setJob ("ON / OFF", "HOLD CLASSIC");
        foot[1]->setJob ("MOMENTARY", "WHAMMY");
        foot[2]->setJob ("NEXT SETTING", "HOLD BACK");
        foot[3]->setJob ("SHIFT LAYER", "HOLD MOMENTARY");
    }
    else
    {
        foot[0]->setJob ("ON / OFF", "HOLD TRILL");
        foot[1]->setJob (glide ? "GLIDE" : "PITCH 1", glide ? String (G.name) + (idx (glideDown) == 1 ? " DOWN" : " UP") : names::pitchName (db::hammerSemis (idx (p1))));
        foot[2]->setJob ("PITCH 2", modeShort[jlimit (0, 4, mode)]);
        foot[3]->setJob ("DROP", D.name);
    }
    for (auto& f : foot) f->repaint();

    String md, sub;
    if (now - proc.uiMessageAt < 2.5 && proc.uiMessage.isNotEmpty()) { md = proc.uiMessage; sub = "PEBBLE IS ON IT"; }
    else if (raw (bypass) > 0.5f) { md = "NAPPING"; sub = "BYPASSED"; }
    else if (eng.uiDetune.load()) { md = "WOBBLING"; sub = "DETUNE"; }
    else if (semis > 0.5f) { md = "STRETCHING"; sub = "UP " + String (semis, 1) + " SEMITONES"; }
    else if (semis < -0.5f) { md = "SQUASHED"; sub = "DOWN " + String (-semis, 1) + " SEMITONES"; }
    else { md = "LISTENING"; sub = noteNameOf (eng.uiFreq.load()); }
    if (md != mood || sub != moodSub) { mood = md; moodSub = sub; repaint (Rectangle<int> (140, 540, 190, 90).transformedBy (AffineTransform::scale ((float) getWidth() / 1260.0f))); }

    const bool bypassed = raw (bypass) > 0.5f;
    bypassButton.set (bypassed ? "OFF" : "ON", "BYPASS", bypassed ? ff::line : ff::ink);
    keepButton.set (proc.learningWhat() == DivebombProcessor::LearnKeeper ? "PRESS IT" : "KEEP", "HOLD C+D", ff::line);

    // Keeper: flips to KEPT, Pebble nods, a note field for ten seconds
    const int kp = proc.uiKeepPulse.load();
    if (kp != lastKeepPulse)
    {
        lastKeepPulse = kp;
        pebble.nod();
        noteNumber = proc.lastKeeper();
        noteEditor.clear();
        noteEditor.setVisible (true);
        noteUntil = now + 10.0;
    }
    if (noteEditor.isVisible() && now > noteUntil && ! noteEditor.hasKeyboardFocus (true)) closeNote (false);

    for (auto& c : footCards) c->tick();
    pebble.update (now, dt);
    expression.repaint();
    footInput.repaint();
    tape.repaint();
}
