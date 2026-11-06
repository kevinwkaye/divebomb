# Divebomb

A pitch plugin for Ableton Live in the Flip Fella family. Two sections, switched on the panel or with footswitches A+B:

- **Treadle**: four treadle pitch pedals in one engine. Pick a model: FIVE, DUAL (adds the drop tune side), NEON (Hypersonic, Octave blends, the new Detune and the neon skin) or BASS (bass lists and voicing). The expression pedal is the treadle.
- **Stomp**: three footswitch pitch pedals. The drop on D, the hammer's Pitch 1 or the glide on B, the hammer's Pitch 2 (hammer, impossible or sequence) on C.

## Download

- Windows VST3: [Divebomb-Windows.zip](https://github.com/kevinwkaye/divebomb/raw/builds/Divebomb-Windows.zip). Unzip and copy `Divebomb.vst3` to `C:\Program Files\Common Files\VST3`, then rescan plugins in Ableton.
- Linux ARM64 (VST3 and LV2, for the Pi rig later): [Divebomb-Linux-ARM64.zip](https://github.com/kevinwkaye/divebomb/raw/builds-arm64/Divebomb-Linux-ARM64.zip).

Each zip has `test-checklist.md` and `ableton-setup.md`. The measured results from the build are in `test-results-windows.md` and `test-results-arm64.md`.

## Footswitches

Defaults: momentary CC 20, 21, 22, 23 on channel 16 for A to D, expression on CC 11. Read straight from the footswitch's USB cable on any channel. Learn remaps anything on the PEDAL page.

| Footswitch | Treadle section | Stomp section |
| --- | --- | --- |
| A | On/off. Hold: Classic or Chords | On/off. Hold: Trill |
| B | Momentary whammy: rises while held, falls on release | Pitch 1, or the glide |
| C | Next setting. Hold: previous | Pitch 2 |
| D | Shift layer. Tap latches, hold is momentary | The drop |
| A+B | Switch section | Switch section |
| B+C held 1 s | Next model | Next model |
| C+D | Next Divebomb. Held 1 s: Keeper | Next Divebomb. Held 1 s: Keeper |

## Latency

Live mode adds no fixed delay: the dry signal and Low Anchor's low end arrive instantly, and the shifted note arrives about one cycle of the note later (2 to 5 ms on guitar notes, 9 to 12 ms on the lowest bass notes). Studio mode is cleaner on chords with a fixed delay Ableton lines up (21 ms, 43 ms in the BASS model). The build measures it every time: see `test-results-windows.md`.

## Building

Everything builds through GitHub Actions on each push to `main`. Locally:

```
cmake -S . -B build
cmake --build build --config Release
build/Release/divebomb_render_test      # bass tracking, levels, footswitches, latency, CPU
```

The DSP engine (`Source/engine`) is plain C++17 with no JUCE, so it builds for Windows x64 and Linux ARM64.

## Credits

Original code. The recreated pedals are by DigiTech; no DigiTech names appear in the plugin. Fonts: Roboto Condensed (SIL Open Font Licence, `Resources/OFL.txt`) and Permanent Marker (Apache Licence 2.0).
