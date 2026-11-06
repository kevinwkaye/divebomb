# Divebomb render test

## Bass tracking

Bend 1 OCT UP at toe in Live Classic. Pitch of the shifted note against a true octave.

| Model | Low B | Low E | A 55 | E 82 | A 110 |
| --- | --- | --- | --- | --- | --- |
| FIVE | -2.0 c | +1.3 c | +1.8 c | +1.3 c | +2.4 c |
| DUAL | -2.0 c | +1.3 c | +1.8 c | +1.3 c | +2.4 c |
| NEON | -2.0 c | +1.3 c | +1.8 c | +1.3 c | +2.4 c |
| BASS | -2.0 c | +1.2 c | +1.7 c | +1.0 c | +2.3 c |

A fading low B, 1.5 s into its decay (about 24 dB down): -1.2 cents.
Bass 1 OCT DN from 82 Hz: -2.2 cents.

## Level

756 renders (every setting of every model, plucked, pad and late start, in all three modes): wet level never more than 20 dB under the input unless listed as a failure.

## Footswitches

Hammer presses land in the buffer they're pressed in. Combos take both presses back. A tap on D latches the shift layer and a hold is momentary.

Treadle swept heel to toe by MIDI every 9 ms: largest step 0.0012 of the travel per 64-sample buffer, 0.000 from toe once the last value arrives.

## Latency

How long after you pluck the shifted note arrives (Bend 1 OCT UP at toe, Low Anchor off), measured on the note's attack. Ableton adds its own buffer on top: 1.3 ms at 64 samples, 2.7 ms at 128, each way. The dry signal and the Low Anchor band arrive with no delay.

| Note | Live Classic | Live Chords | Studio | Studio, Bass model |
| --- | --- | --- | --- | --- |
| Low B (31 Hz) | 11.7 ms | 9.2 ms | 20.6 ms | 43.5 ms |
| Low E (41 Hz) | 8.9 ms | 9.1 ms | 21.7 ms | 43.9 ms |
| Guitar low E (82 Hz) | 6.2 ms | 12.5 ms | 24.0 ms | 42.6 ms |
| A (110 Hz) | 4.8 ms | 5.9 ms | 21.6 ms | 39.0 ms |
| E (330 Hz) | 2.7 ms | 4.2 ms | 20.6 ms | 41.8 ms |
| A (440 Hz) | 2.1 ms | 4.2 ms | 20.8 ms | 42.3 ms |

## CPU

One instance at 48 kHz, the heaviest setting (NEON octave morph with the shift layer on, four voices), as a share of one core on the machine that ran this test.

| Buffer | Live Classic | Live Chords | Studio |
| --- | --- | --- | --- |
| 64 samples | 0.9% | 1.6% | 2.4% |
| 128 samples | 0.9% | 1.6% | 2.5% |

All checks passed.
