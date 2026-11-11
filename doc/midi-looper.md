# Live MIDI looper

The live looper records a beat-synchronized phrase, repeats it, adds overdubs
and offers one-level overdub Undo. All looper pages control the same session.
Ordinary [MIDI file patches](midi-files.md) remain separate operations.

```toml
[pages.clock]
type = 'clock-transport'

[pages.loop]
type = 'midi-looper'

[page_settings.looper]
inputs = ['din']
outputs = ['din']
channels = [1]
events = 512
```

Use declared MIDI endpoint names. Inputs default to all enabled inputs,
channels to all channels, and outputs to the target's default MIDI output.
A looper page supplies these defaults if its settings table is omitted.
The normal compile, editable export, setup bundle and history paths carry
this configuration. Saving a setup does not save the current performance.

## Playing and recording

Start the selected clock before recording or playing. The clock transport page
can start the internal clock without sending clock messages to an instrument;
`page_settings.clock_outputs` determines which outputs receive those messages.
An external clock must be receiving ticks and have a measured tempo.

The page uses the target's logical control roles:

| Control role | Action |
| --- | --- |
| Previous | Record the first take, finish it, arm an overdub or finish an overdub. |
| Next | Play or Stop. |
| Confirm | Undo the last committed overdub. |
| Previous section | Open the Clear confirmation. Confirm clears; Back cancels. |
| Next section | Open Save/Load. Previous/Next choose; Confirm accepts. |
| Back | Cancel a menu, or use normal page navigation. |

The first Record starts on the next quarter-note beat. Record again to finish
on the next beat, at least one beat after the start. Silence is valid and remains
part of the loop. Live events use the shared beat position; saved files use
3840 PPQN.

The first take fixes the length. Record while playing arms an overdub for the
next complete loop boundary. It may run across several passes; Record again
closes it at a loop boundary. The old loop keeps playing during preparation.
Publication and Undo switch at a subsequent boundary, preserving the absolute
phase. Undo is available only for the last completed overdub, without redo.
Undo while stopped takes effect immediately.

Stop discards unfinished capture and releases only the loop's notes and pedal
holds. A stopped committed loop remains available. Page navigation leaves it
running. Setup replacement, stock firmware and port-owning diagnostics stop
the session and wait for its cleanup before taking over the ports.

Tempo changes follow the shared clock without resetting phase. MIDI Stop pauses
a transport-armed loop and cancels unfinished capture. Continue schedules the
committed loop at the next whole loop boundary. Start resets its origin.
Explicit looper Stop disarms automatic transport restart. Clock loss stops the
loop and requires an explicit Play after recovery.

## Patch bindings

Page controls and patch bindings use the same session commands. A command
returns acceptance or an error immediately; the patch executor does not wait
for the loop to finish.

```toml
[patches.record-loop]
operation = 'midi-looper'
action = 'record'

[patches.load-loop]
operation = 'midi-looper'
action = 'load'
filename = '42-1.mid'
```

Actions are `record`, `play-stop`, `undo`, `clear`, `save` and `load`. Only Load
takes a filename, which must be a basename. Explicitly bound Clear and Load
execute immediately, including replacement of unsaved work. Assign them to an
appropriate gesture. The page's Clear and replacement Load controls ask for
confirmation. Bindings can use `page_settings.looper` without a looper page.

## Capacity and delivery

The initial design is for notes and pedals over at most 64 quarter-note beats,
equivalent to 16 bars in 4/4. It has no time-signature or bar model.

The default limit is 512 MIDI messages per generation, including terminal
releases and expanded parameter writes. A fine-CC write uses two messages and
a complete parameter write uses five or six. The committed loop plus a candidate
must fit the event limit. There are at most 32 simultaneous notes/holds,
32 controller decoding contexts and one active, previous and candidate version.
Reaching a limit aborts the candidate with an error and preserves the committed
loop. It does not silently drop releases or truncate a phrase.

Live generations and prepared indexes reside in RAM2. The default shared budget
is 113,344 bytes, including recorder/import allowances and allocator overhead.
It is charged once, regardless of the number of views. `events` accepts 1 to
4096, but a setup must fit the target's complete memory budget, including other
MIDI engines, fonts and documents. Large combinations may be rejected.

Capture observes selected external input once and adds no MIDI thru path.
Monitoring uses the setup's existing routing. USB cables and input sources keep
separate note pairing. Running status continues across arming. A source change
discards incomplete controller state and aborts a take if that source still has
captured held notes.

Notes, controllers, pressures, pitch bend and Program Change are captured.
System messages, transport and SysEx are excluded. Orphan releases do not create
voices; remaining notes and CC 64..69 holds close at the boundary without ties
between cycles. Ownership protects other MIDI sources sent through the
controller. Program/bank changes are deliberate events; Stop does not restore
an instrument's previous program.

Fine-CC and NRPN/RPN writes are prepared as atomic groups, including selector
nulling. Their completed value arrives at the final data message's musical
time. Coarse values without a following LSB remain coarse writes. An orphan
LSB, data without captured selectors or an incomplete selector group is
discarded and counted. Pairing uses a two-millisecond window at the take's
initial tempo. Completed parameter selections persist across overdub passes;
incomplete transactions are discarded at their boundaries. Output locks keep
another source from interleaving a group.

An output that misses a cycle retires independently. Healthy outputs retain
the global phase; stale onsets are not replayed in later cycles. Required
cleanup continues for a blocked destination. A replacement device does not
receive its predecessor's cleanup, and recovered destinations require a fresh
Play to rejoin.

## Saving and loading

Save pins the committed generation and snapshots the most recent valid selected
tempo. Playback can continue. Record, Undo and replacement jobs are rejected
until Save completes. Files use ordinary SMF type 0, 3840 PPQN and the existing
exclusive startup-counter naming rules in the MIDI directory. Existing files
are never overwritten. A failed save removes only its newly created file.

The picker scans one directory entry per service visit and skips unfinished
files produced by the recorder. Load accepts SMF types 0 and 1 with PPQN timing,
at most 16 tracks and the same event/held-note limits. It preserves positive
end-of-track duration and fractional beats instead of rounding to the command
grid. Export to 3840 PPQN rounds by at most one output tick. Embedded tempo does not take
over the selected clock. SMPTE timing and unsupported performance messages are
rejected before replacing the working loop. A failed Load retains the old loop
stopped. Load never starts playback automatically.

Recording and looping share the primary file player/recorder lease. Ordinary
file patches report Busy while the looper owns it. Independent stream-file
generators retain their own players. Save/Load also serialize against setup,
font, movie and other storage transfers. Reboot discovers completed saved files;
it does not restore or automatically play unsaved RAM2 generations.

## Timing and qualification

Capture maintains an ordered prepared index. First-wrap playback uses it before
the common SMF writer finishes, without an SD read, write or sync at the boundary.
The scheduler rejects an initial launch already more than one millisecond past
its requested boundary. Later boundaries use the absolute clock origin and
length, with no cumulative delay from previous delivery.

`CNTRL MIDI LOOPER` reports state, error, capacity, high-water counts, length,
late boundaries, maximum observed boundary lateness, retired outputs and
discarded groups. Use `CNTRL MIDI STATUS <route>` for the existing maximum MIDI
service and input-scan gaps.

Host tests cover phase, ownership, capacity, storage failures and transport
behavior. Physical first-wrap latency, jitter and receiver behavior still need
the independently timestamped tests in [the qualification plan](../plan/looper.md).
Explicit Save/Load uses synchronous storage calls and can introduce latency;
host tests do not establish its timing on a real card.
