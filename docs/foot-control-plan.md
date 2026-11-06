# Divebomb foot control plan

Divebomb copies `PedalControl` (with `DirectMidiInput`) and `Keeper` from Prism Patch. One change to `PedalControl`: `FootswitchLogic` adds a B+C combo (`ComboBC`, `ComboHoldBC`) alongside A+B and C+D, for stepping the model. The other plugins can take it up unchanged.

## Where each job runs

Pitch jobs run on the audio thread inside the engine, so a hammer-on or drop lands in the buffer you pressed it in (under 3 ms at 128 samples), not on the 100 Hz message timer:

- Treadle: B (momentary whammy), D (shift layer)
- Stomp: B (Pitch 1 or the glide), C (Pitch 2), D (the drop)

Everything else runs on the message thread through `FootswitchLogic`: A, C in the Treadle section, and all combos.

Both sides use the family timings: hold 0.45 s, combo window 0.12 s, combo hold 1 s. When a second switch joins within the combo window, the engine takes back both pitch jobs and the message thread takes back A's bypass toggle (the Speckle `revertPress` pattern), so A+B, B+C and C+D never leave a stray jump or toggle behind.

## Table

| Footswitch | Treadle tap | Treadle hold | Stomp tap | Stomp hold |
| --- | --- | --- | --- | --- |
| A | On/off, on the press | Classic or Chords (the press's bypass toggle is taken back) | On/off | Trill on or off |
| B | Momentary whammy: rises at Rise time while held, falls at Fall time | (the hold is the job) | Pitch 1 while held, or the glide | (the hold is the job) |
| C | Next setting, on release | Previous setting | Pitch 2: second hammer, alternating impossible pitches, or the next sequence step | (the hold is the job) |
| D | Shift layer flips on the press. Released before 0.45 s it latches | Momentary: lets go on release | The drop while held (or latch, set on screen) | (the hold is the job) |
| A+B | Switch section | | Switch section | |
| B+C | | Held 1 s: next model | | Held 1 s: next model |
| C+D | Next Divebomb | Held 1 s: Keeper | Next Divebomb | Held 1 s: Keeper |

## Expression

CC 11 drives the treadle on the audio thread, with the shared heel and toe calibration and curve. MIDI arrives in 128 steps, so the engine glides between values over the gap they arrived in (2 to 30 ms): no staircase and no added lag. The last 2% at each end snaps to exactly heel or toe. In the Stomp section the expression pedal sets the trill rate by default. "Expression moves" on the PEDAL page assigns it to the treadle, trill rate, glide shift, glide return or level.

## Original pedal MIDI map

Off by default. On a chosen channel: program changes 1 to 21 pick a setting in Classic, 22 to 42 the same bypassed, 43 to 63 in Chords, 64 to 84 bypassed. CC 11 moves the treadle.
