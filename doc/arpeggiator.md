# MIDI arpeggiator

The arpeggiator plays entries from a bank on a beat grid shared with Tap Tempo
and MIDI Clock. Choose a held chord in poly mode or a recorded sequence of
complete note gestures in mono mode. Every arpeggiator page controls the same
session, which continues across page changes.

```toml
[pages.arp]
type = 'arpeggiator'

[page_settings.arpeggiator]
mode = 'poly'
inputs = ['din']
outputs = ['usb-device']
channels = [1]
events = 128
selection = {kind = 'alternating', repeat_begin = false, repeat_end = true}
rhythm = {kind = 'grid', step = '1/4 beat'}
gate = '4/5'
seed = 42
```

Use MIDI endpoint names declared by the target. Inputs default to all enabled
inputs, outputs to the target's default MIDI output, and channels to all
channels. A page supplies the default shared configuration if its settings
table is omitted. The compiler accounts for bounded capture and routing storage
in the setup's RAM2 budget. `events` defaults to 128 and can range from 1 to
4096 subject to the target's memory allowance. Configuration survives editable
export and setup bundling. Performance banks reset on setup changes.

## Poly controls

The logical Next switch toggles On/Off. On starts a stopped internal clock and
any configured outgoing MIDI Clock. In external mode it waits for a running
external clock. Hold a chord on the configured inputs. Entries are ordered by
pitch, with input/cable and channel distinguishing equal pitches. Releases,
including velocity-zero Note On, remove entries. Releasing the selected entry
releases its generated note.

While enabled, the arpeggiator replaces live notes on its configured outputs.
Other MIDI routes can continue to use their own outputs and processors. The
arpeggiator captures original input pitches and sends directly to its configured
outputs. Off releases generated notes and restores held notes immediately.
Releases and restoration retry if an output temporarily rejects a write.

## Mono controls

Set `mode = 'mono'` and use `selection = {kind = 'played'}` for forward order,
or `selection = {kind = 'played', direction = 'reverse'}` for reverse order.
The other selectors work on ranks in the recorded order. Omitting selection in
mono mode chooses forward played order.

Start the selected clock before recording. The logical Previous switch arms
Record for the next beat. Press Previous while recording to arm Finish for
the next beat. Each attack starts a segment and its release completes it.
Legato attacks close the previous segment at the new attack. Old releases,
including repeated-pitch releases, are matched in onset order. Finish appends
a matching Note Off to an unfinished final segment at the Finish boundary.
Silence between notes creates no segment.

Finish publishes the new phrase and starts playback on a grid boundary. Next
stops recording or playback, or starts the last committed phrase when stopped.
A cancelled or overflowing take preserves that committed phrase. Recording a
new phrase replaces it on successful Finish.

Segments preserve velocity, release velocity, pitch bend, pressure and continuous
controllers. Known expression at onset is replayed before the note attack.
Recorded expression is scaled into the selected segment's sounding time.
Paired CC and parameter messages use the shared capture decoder and protected
output groups. Reordering changes segment order while preserving the events
inside each gesture.

## Rhythm and selection

`rhythm.step` is a positive rational duration up to 64 beats. `gate` is a rational
fraction greater than zero through one. At 120 BPM, `step = '1/4 beat'` gives
125 ms between attacks and `gate = '4/5'` gives 100 ms of sounding time.
Tap-tempo changes preserve beat phase. A late service call skips expired attacks
and keeps releases ahead of new attacks.
An explicit transport Stop switches poly mode Off and restores the held chord.
If external clock ticks stop arriving, playback continues at the last measured
tempo until a transport command or switch changes it.

| Selection | Configuration and order |
| --- | --- |
| Forward | Poly `kind = 'ascending'`, mono `kind = 'played'`. |
| Reverse | Poly `kind = 'descending'`, mono `kind = 'played', direction = 'reverse'`. |
| Back and forth | `kind = 'alternating'`. `repeat_begin` and `repeat_end` independently duplicate their turnarounds. Both default to false. |
| Random | `kind = 'choice'`. Draws from the whole bank, allowing consecutive repeats. |
| Random walk | `kind = 'walk', cycle = false, prob_up = '1/2', prob_down = '1/2'`. Unused probability stays on the same entry. `cycle = true` wraps. False reflects at the ends. |
| Mod | `kind = 'mod', modulus = 2`. Visits 0,2,4,1,3,5 for six entries. Modulus 3 visits 0,3,6,1,4,7,2,5,8 for nine. |
| Shuffle | `kind = 'shuffle'`. Visits every rank once in a fresh random order each cycle. |
| Outside in | `kind = 'outside_in'`. Alternates low and high ranks, such as 0,4,1,3,2. |

Modulus is an integer from 2 through 4096. Walk probabilities are nonnegative
rational strings whose sum is at most one. `seed` is a nonzero 32-bit integer
used by a local generator for repeatable random decisions. An empty bank rests
and a single entry repeats once per hit. A surviving selected poly entry keeps
its rank relationship through chord edits. Removing it restarts selection.

Automated checks cover selection traces, configuration round-trips, note
handoff, recorded expression and clock phase. Hardware timing and musical
qualification remain outstanding.
