# MIDI LFOs

Use an `lfo` generator in a [MIDI route](midi-processing.md) to vary a typed
parameter. System routes start with the setup and survive patch changes. Patch
routes start when their patch is selected and stop when it is left. Selecting
the same patch preserves its running state.

```toml
[patches.vibrato.midi.bend]
to = 'din'
generate = {lfo = {
    target = {type = 'pitch-bend', channel = 1},
    shape = 'sine',
    range = [-512, 512],
    period = '200 ms',
    off = {action = 'value', value = 0},
}}

[midi.filter]
to = 'trs'
generate = {lfo = {
    target = {type = 'control-change', channel = 1, controller = 1, resolution = 14},
    shape = 'smooth-random',
    range = [2048, 14335],
    period = {beats = '4'},
    seed = 7,
    off = {action = 'hold'},
}}

[patches.stop_filter]
operation = 'midi-stream'
stream = 'system/filter'
enabled = false
```

Endpoint aliases must exist in the target's registry. A bare stream name gates
the selected patch's route; `system/name` gates a system route. Repeatedly
enabling an enabled route does not restart it. Set `enabled = true` to start a
new activation after stopping or completing a one-shot.

## Targets and values

Each target requires `type` and `channel` (1..16), plus these addressing fields:

| Type | Address | Range |
| --- | --- | --- |
| `control-change` | `controller`; `resolution = 7` by default | 0..127 |
| `control-change` | MSB `controller` 1..31, `resolution = 14` | 0..16383 |
| `pitch-bend` | None | -8192..8191, neutral 0 |
| `channel-pressure` | None | 0..127 |
| `polyphonic-key-pressure` | Fixed `note` 0..127 | 0..127 |
| `nrpn` | `parameter` 0..16383; `resolution = 14` by default, or 7 | 0..16383 or 0..127 |
| `rpn` | `parameter = 1`, optional `resolution = 14` | 0..16383, neutral 8192 |
| `program-change` | None; uses the current bank | 1..128 |

Fine CC controller 6 is excluded. Standalone CC destinations also exclude bank
selection (0, 32), data entry (6, 38), switches (64..69), portamento control
(84), high-resolution velocity prefix (88), increment/decrement (96, 97),
parameter selection (98..101), and channel-mode controllers (120..127).
Other controller numbers are numeric addresses; choose a receiver parameter
that actually supports continuous values. NRPN interpretation is instrument
specific. RPN supports channel fine tuning only.

Pressure does not create notes or follow held voices. Pitch bend uses the
receiver's existing bend range. Program Change selects numbered programs in the
current bank, without sending bank selection. Quantization and congestion can
skip intermediate programs. A bank change invalidates a captured program
restore value.

`target`, `shape`, two-element `range`, `period` and `off` are required.
Descending range endpoints invert the shape; equal endpoints produce a constant.
Values stay at native resolution through processing and are rounded once to
the nearest integer, with ties toward positive infinity. Fine CC emits its MSB
then LSB. NRPN and RPN emit complete selection/data-entry groups followed by
RPN Null. Only MIDI real-time traffic can interleave with a started group.

## Timing and shapes

`period` accepts a duration from `1 ms` to `1 h`, or `{beats = '1/2'}` for half
a quarter-note beat. Beat periods are positive exact fractions, at most 4096
beats with denominator at most 65535. `update_interval` defaults to `10 ms`
and accepts `1 ms` through `60 s`. It sets the earliest ordinary sample spacing;
wire throughput and scheduler load determine actual delivery spacing.

| Shape | Cycle |
| --- | --- |
| `sine` | Midpoint, rising, maximum, midpoint, minimum, midpoint |
| `triangle` | Minimum, maximum at half-cycle, minimum |
| `saw-up` | Minimum to maximum, then wrap |
| `saw-down` | Maximum to minimum, then wrap |
| `square` | Minimum for half a cycle, maximum for half |
| `random` | One seeded value held for each cycle |
| `trapezoid` | Rise, high plateau, fall, low plateau in equal quarters |
| `smooth-random` | Smoothstep interpolation between seeded cycle values |

`phase` defaults to 0 and is a fraction in [0, 1). `phase_mode = 'retrigger'`
starts at that phase on each activation. `phase_mode = 'free'` advances while
gated off, anchored to the route's scope activation. Leaving a patch discards
its anchor. Random shapes accept a 32-bit unsigned `seed`, default 0. Retrigger
repeats the seeded pattern. The integer hash repeats after 2^32 cycles; sine
uses a committed integer lookup table so hosts and firmware agree.

`run = 'repeat'` is the default. `run = 'once'` requires retrigger mode and
runs one period, delivers the terminal value, applies the off policy and gates
itself off. A rising saw at phase 0 ends at its maximum before wrapping.
Initial, terminal and final writes retain their order even after a long poll gap.

Duration timing follows elapsed time regardless of transport. Beat timing
follows the controller's selected clock, including when no clock output is
selected. Stop holds phase; Continue resumes; Start resets enabled runs to
their authored phase. External-clock loss pauses the beat position and reports
waiting, without a fallback tempo. Tempo changes preserve phase.

## Stopping, ownership and delivery

Choose an explicit off policy:

| Policy | Result |
| --- | --- |
| `{action = 'hold'}` | Leave the last delivered value |
| `{action = 'value', value = 0}` | Send the specified legal value once |
| `{action = 'restore'}` | Restore each output's captured previous value |

Restore requires a known, fully accepted parameter value on every participating
output before activation. An unknown snapshot fails the activation. This state
records writes accepted by the controller's output driver; it cannot establish
the instrument's actual state after an external edit.

Off cancels unsent waveform values and delays, finishes a started transaction,
then applies its policy. Final values use the destination's native units and
bypass value processors and delays. Cleanup survives patch/setup replacement.
A new activation waits for required old cleanup. Repeated off requests do not
queue more cleanup.

Only one LFO can own an output/channel/parameter. Compilation rejects competing
LFOs, including overlapping coarse/fine CCs and system/patch routes. A manual
or patch write retires the conflicting LFO writer before taking ownership.
An instrument program change retires other parameter owners on that channel.

LFO routes support filtering, full-value scale/map, fixed address mapping,
key-pressure transpose/invert, a single delay and live reverse windows.
Dynamic or conditional address changes, reconverging branches, repeated echoes,
and packet-based extension processors are rejected because cleanup needs a
single known destination parameter. Match `parameter` for typed NRPN/RPN values.

Phase advances independently of blocked outputs. Each output retains bounded
initial/terminal/final state and one coalesced latest ordinary value, suppressing
duplicate delivered values. A slow output does not stall healthy outputs.
Disconnect cancels ordinary modulation and invalidates partial transactions.
Cleanup is tied to the saved connection identity and is never sent to a
replacement. A returning destination requires a fresh off/on activation to
join; it receives no modulation backlog. Passive DIN/TRS disconnection cannot
be detected reliably. Bytes already delivered before a disconnect cannot be
undone.

## Limits and diagnostics

A compiled graph supports at most 16 LFO generators and 128 active destination
bindings, within the existing 64 routes, 16 steps per route, 512 events and
256 capture slots. Cleanup and active deliveries share 256 bounded records.
Ordinary samples are coalesced under load; round-robin delivery prevents a
busy parameter from monopolizing an output.

The setup memory estimate reserves a 192 KiB stream pool for LFO graphs,
plus 4096 bytes and four times the compiled stream-section size, plus extension
memory. Non-LFO graphs retain their existing estimate. Native stress tests
exercise 16 LFOs with routed notes and a blocked destination, checking retained
container storage against that allowance with 32 KiB reserved for other work.
This is an allocation bound for the exercised graph, not a measured hardware
heap high-water mark or a guarantee of a particular update rate.

`CNTRL MIDI STREAMS` appends `lfos`, `pending`, `stopping`, `waiting`, `missing`,
`failed`, `coalesced`, `skipped` and `bytes` to its existing counters. `failed`
is a route bitmask; `bytes` reports retained stream-container storage, excluding
opaque extension allocations and transient standard-library workspace.

Host/native tests and target compilation do not qualify musical smoothness.
Receiver resolution, spacing, jitter and latency during scrolling and transfers
still require an independently timestamped physical MIDI receiver.
