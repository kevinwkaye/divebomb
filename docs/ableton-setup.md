# Setting up Divebomb in Ableton

1. Unzip `Divebomb-Windows.zip` and copy `Divebomb.vst3` to `C:\Program Files\Common Files\VST3`.
2. In Ableton, Preferences, Plug-Ins: turn on VST3 and press Rescan.
3. Put Divebomb first on your guitar or bass track, before any drive. Pitch shifters track best on the clean signal.
4. Set the track's monitoring to In and play.

## Footswitches and expression

Divebomb reads your footswitch straight from its USB cable, so there's no MIDI track to set up.

1. Plug the footswitch in by USB.
2. In Ableton, Preferences, Link, Tempo and MIDI: find the footswitch under MIDI Ports and turn Track, Sync and Remote off for its input. Windows only lets one program read a MIDI device at a time.
3. Open Divebomb. The strip says FOOTSWITCHES READY, and the PEDAL page shows a green dot next to FOOTSWITCHES.
4. On the PEDAL page, click EXPRESSION, choose Calibrate heel and toe, then rock the pedal heel to toe within six seconds. This also fixes a pedal that reads backwards.

The defaults are CC 20 to 23 for A to D and CC 11 for expression. If your footswitch sends something else, press LEARN on each card and tap the switch.

If you'd rather route MIDI through Ableton: on the PEDAL page, click FOOTSWITCHES and choose Only from a MIDI track in Ableton, then make a MIDI track with your footswitch as input and Divebomb's track as output.

## Monitoring latency

In Live mode Divebomb adds no fixed delay. Ableton's own buffer still adds 1.3 ms each way at 64 samples, 2.7 ms at 128. Studio mode reports its delay to Ableton. Turn on Options, Reduced Latency When Monitoring so other plugins' delay stays off your monitored track.

## Kevin's setup

Chocolate Plus in CubeSuite Custom mode, A to D on CC 20 to 23, the expression jack on CC 11, switch on U for USB MIDI. It always sends on channel 1, which doesn't matter when it's read directly. The EX-P needs a 3.5 mm to 6.35 mm TRS adapter and reads backwards until you calibrate.
