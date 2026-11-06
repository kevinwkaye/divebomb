// Renders the panel to PNG files for checking the layout: treadle, stomp, neon, mid-wipe, deep and pedal pages.
// Not part of the release build: cmake -DDIVEBOMB_BUILD_SNAPSHOT=ON, then run with an output folder.
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static void set (DivebombProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static void run (DivebombProcessor& p, int ms, float freq = 110.0f)
{
    AudioBuffer<float> b (2, 128);
    MidiBuffer midi;
    static double ph = 0;
    const int blocks = ms * 48 / 128;
    for (int k = 0; k < blocks; ++k)
    {
        for (int i = 0; i < 128; ++i) { const float v = 0.3f * (float) std::sin (ph); ph += 2.0 * MathConstants<double>::pi * freq / 48000.0; b.setSample (0, i, v); b.setSample (1, i, v); }
        p.processBlock (b, midi);
        if (k % 3 == 0) MessageManager::getInstance()->runDispatchLoopUntil (8);   // roughly real time for the panel
    }
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}

static void shot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream out (f);
    PNGImageFormat().writeImageToStream (img, out);
    std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const File dir (argc > 1 ? String (argv[1]) : File::getCurrentWorkingDirectory().getFullPathName());
    DivebombProcessor p;
    p.prepareToPlay (48000.0, 128);
    std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
    ed->setVisible (true);

    set (p, "model", 1); set (p, "setting", 1); set (p, "treadle", 0.7f); set (p, "layerOn", 1);
    run (p, 800);
    shot (*ed, dir.getChildFile ("1-treadle-dual.png"));

    set (p, "section", 1); set (p, "hamMode", 2); set (p, "trill", 1);
    run (p, 400);
    p.footDown (3); run (p, 300);
    shot (*ed, dir.getChildFile ("2-stomp.png"));
    p.footUp (3);

    set (p, "jump", 1); set (p, "hamMode", 1);
    run (p, 300);
    shot (*ed, dir.getChildFile ("3-stomp-glide.png"));

    set (p, "section", 0); set (p, "model", 2); set (p, "setting", 18); set (p, "treadle", 0.4f);
    run (p, 150);
    shot (*ed, dir.getChildFile ("4-neon-wipe.png"));
    run (p, 1500);
    shot (*ed, dir.getChildFile ("5-neon-treadle.png"));
    set (p, "section", 1); set (p, "jump", 0); set (p, "hamMode", 0);
    run (p, 400);
    shot (*ed, dir.getChildFile ("6-neon-stomp.png"));
    set (p, "model", 3); set (p, "section", 0); set (p, "setting", 9); set (p, "treadle", 1.0f); set (p, "input", 1);
    run (p, 1500, 41.2f);
    shot (*ed, dir.getChildFile ("7-bass-divebomb.png"));
    return 0;
}
