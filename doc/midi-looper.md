# Live MIDI looper

The live looper records a beat-synchronized phrase, repeats it, adds overdubs
and offers one-level Undo for overdubs and Reverse edits. All looper pages
control the same session.
Ordinary [MIDI file patches](midi-files.md) remain separate operations.

```toml
[pages.loop]
type = 'midi-looper'

[pages.time]
type = 'time'

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

Start the selected clock before recording or playing. Select DIN or TRS on
Time to follow that input; it must be receiving ticks and have a measured tempo.

The page uses the target's logical control roles:

| Control role | Action |
| --- | --- |
| Previous | Record the first take, finish it, arm an overdub or finish an overdub. |
| Next | Play or Stop. |
| Confirm | Undo the last committed overdub or Reverse edit. |
| Previous section | Open the Clear confirmation. Confirm clears; Back cancels. |
| Next section | Open More: Save, Load, Reverse phrase or Toggle direction. Previous/Next choose; Confirm accepts. |
| Back | Cancel a menu, or use normal page navigation. |

The first Record starts on the next quarter-note beat. Record again to finish
on the next beat, at least one beat after the start. Silence is valid and remains
part of the loop. Live events use the shared beat position; saved files use
3840 PPQN.

The first take fixes the length. Record while playing arms an overdub for the
next complete loop boundary. It may run across several passes; Record again
closes it at a loop boundary. The old loop keeps playing during preparation.
Publication and Undo switch at a subsequent boundary, preserving the absolute
phase. Undo is available only for the last completed overdub or Reverse edit,
without redo.
Undo while stopped takes effect immediately.

Reverse phrase mirrors the stored phrase in time. Note durations and onset
velocities are preserved, sustain/hold transitions run backward, and fine-CC
and NRPN/RPN transactions retain their internal byte order. Other continuous
values and Program Changes retain their values at mirrored times. Save writes
the edited phrase, and Undo restores its previous version. Reversing a silent
phrase preserves its duration. Preparation uses compact in-memory generations.

Toggle direction switches forward/backward playback without editing the phrase,
consuming Undo or marking it unsaved. Save ignores playback direction. The page
shows Forward or Backward and any pending direction change. Overdubbing is
unavailable during backward playback; switch back to Forward first. Undo and
Reverse edits remain available, and direction applies independently to the
current phrase. Stop retains direction; Clear and a successful Load reset it.

Both operations switch at a subsequent whole loop boundary during playback,
after the current cycle's note/hold cleanup. While stopped they need no clock
or boundary; a Reverse edit completes its bounded RAM preparation before the
phrase becomes available. Conflicting capture/edit commands are rejected.

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

Actions are `record`, `play-stop`, `undo`, `clear`, `save`, `load`, `reverse` and
`direction`. `reverse` edits the phrase; `direction` toggles playback. Only Load
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
must fit the event limit. There is one active, previous and candidate version.
Held-note and controller-decoder storage grows with actual capture activity;
outstanding notes reserve room for their terminal releases within the limit.
Reaching a limit aborts the candidate with an error and preserves the committed
loop. It does not silently drop releases or truncate a phrase.

Live generations store packed MIDI messages and a four-byte integer musical
timestamp per message. For three-byte messages, 24 events use 168 payload bytes
and 512 use 3,584. Allocation headers, small seek checkpoints, controller-group
metadata and spare capacity are additional. Reverse playback adds a compact
velocity/hold cache when needed. Capture, output accounting, destination
controller values and retained Undo generations also consume memory.

`events` accepts 1 to 65535 and is a maximum, not an upfront reservation.
Recording starts small and grows through the shared checked allocator. Actual
available memory, including other MIDI engines, fonts and documents, limits
practical capacity. All views share one session and its storage.

Capture observes selected external input once and adds no MIDI thru path.
Monitoring uses the setup's existing routing. USB cables and input sources keep
separate note pairing. Running status continues across arming. A source change
discards incomplete controller state and aborts a take if that source still has
captured held notes.

Notes, controllers, pressures, pitch bend and Program Change are captured.
System messages, transport and SysEx are excluded. Orphan releases do not create
voices; remaining notes and CC 64..69 holds close at the boundary without ties
between cycles. Each output source retains its own outstanding Note On counts.
Repeated attacks and their releases are forwarded without cross-source
suppression. Stop sends one Note Off per remaining attack and releases the
loop's held pedals. A receiver cannot identify which source a Note Off belongs
to, so these messages can affect live notes or pedals sharing its channel.
Program/bank changes are deliberate events; Stop does not restore an
instrument's previous program.

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

## Continuous controls when stopping

`loop_stop_reset_controls = false` is the setup-wide default. Stopping a loop
always sends its outstanding note releases and held-pedal releases, while
leaving pitch bend, pressure and other continuous controls unchanged.

Set `loop_stop_reset_controls = true` to also reset controls actually sent by
the stopping loop. Pitch bend returns to center and channel/key pressure to
zero. Other controls reset only when listed in `loop_stop_neutral_values`;
unlisted controls retain their last values. Program and bank selections are
not reset. These writes can affect live playing or another source sharing the
same destination and channel; the pedal does not restore historical values.

```toml
loop_stop_reset_controls = true
loop_stop_neutral_values = [
    {type = 'control-change', output = 'din', channel = 1, controller = 10, value = 64},
    {type = 'nrpn', output = 'din', channel = 1, parameter = 42, value = 8192},
]
```

Each neutral declaration requires an explicit output and channel. Use the
ordinary `control-change`, `nrpn` or `rpn` message fields, including
`resolution = 14` for paired CC or `resolution = 7` for coarse NRPN.
Neutral values match the underlying parameter even when playback uses a
different resolution. A paired CC write can touch both separately configured
seven-bit halves. Overlapping declarations, such as a paired CC neutral and
another neutral for either of its halves, are rejected.

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
it does not restore or automatically play unsaved in-memory generations.

## Timing and qualification

Capture maintains the compact phrase directly. First-wrap playback reads it
without building an SMF or doing an SD read, write or sync at the boundary.
The scheduler rejects an initial launch already more than one millisecond past
its requested boundary. Later boundaries use the absolute clock origin and
length, with no cumulative delay from previous delivery.

`CNTRL MIDI LOOPER` reports state, error, capacity, high-water counts, length,
late boundaries, maximum observed boundary lateness, retired outputs and
discarded groups. Use `CNTRL MIDI STATUS <route>` for the existing maximum MIDI
service and input-scan gaps. The status also reports `backward` and
`direction_pending` flags.

Host tests cover phase, ownership, capacity, storage failures and transport
behavior. Physical first-wrap latency, jitter and receiver behavior still need
the independently timestamped tests in [the qualification plan](../plan/looper.md).
Explicit Save/Load uses synchronous storage calls and can introduce latency;
host tests do not establish its timing on a real card.
