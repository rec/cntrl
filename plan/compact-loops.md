# Compact loop storage and unified note accounting

Status: proposed, 2026-11-15. This document plans a replacement of the current
in-memory representation and accounting. It does not implement the replacement.

## Objective and agreed behaviour

Make short loops cheap and make memory use follow recorded data and outstanding
notes, rather than reserving several worst cases for every configured event.

- Store complete MIDI messages in a packed byte stream, with one four-byte
  integer musical timestamp per message. Do not use running status internally.
- Forward every accepted Note On and Note Off independently of other sources.
  Remove cross-source suppression for both notes and held pedals.
- Keep one shared accounting system for outstanding output notes, distinguished
  by source, destination, channel and pitch. Each loop has its own identity.
- On stopping a loop, send its outstanding releases. MIDI does not identify a
  source in a Note Off; the instrument decides which voice that release affects.
- Make continuous-control cleanup globally configurable. The default releases
  notes and held pedals but leaves continuous values unchanged. The optional
  policy resets only controls actually sent by the stopping loop.
- Preserve tap-tempo synchronization, quantized commands, overdub, Undo, Reverse,
  playback direction, atomic controller transactions and SMF Save/Load.

This does not add multiple simultaneous loop sessions or new loop controls.
The representation and source identities must avoid assumptions that would
force each future loop to reserve another full global state table.

## Current costs and causes

The existing arm path in `firmware/include/cntrl/midi_looper.h` reserves a
RAM-backed SMF, a 32-byte-per-event index, a second full index for editing,
ordering storage, held-note records, a full-capacity recorder queue, global
playback credits and note ownership credits before capture starts.

For 512 events, one physical input, all 16 channels and one output, the current
source-derived estimate is approximately 172.5 KiB from otherwise unreserved
storage. This is a calculation using ARM structure sizes and normal allocation
charges, not a new hardware measurement. Other input cables, outputs and existing
reservations change the result.

| Allocation | Approximate current reservation |
| --- | ---: |
| SMF bytes | 4.1 KiB |
| Event index and editing scratch | 32 KiB |
| Ordering and held records | 6 KiB |
| Recorder queue | 32 KiB |
| Global playback state credits | 61.4 KiB |
| Global ownership credits | 36 KiB |
| Decoder state and small bookkeeping | 0.9 KiB |

Playback credits reserve event-count-sized note, controller and parameter lists
simultaneously. Linked-list nodes, separate allocations and allocator headers
make small records expensive. None of these quantities is the MIDI payload size.
The Python estimator in `cntrl/patch_switcher/looper.py` must be replaced with an
estimate of the new allocations, not retained as an authoritative baseline.

## Compact generation format

A generation owns a packed MIDI byte buffer, a parallel `uint32_t` time buffer,
event and byte counts, and its musical duration. Store one-, two- and three-byte
channel messages at their actual lengths, with a status byte on every message.
Continue excluding SysEx and system messages from live loops.

Use 2^24 ticks per quarter-note beat, relative to the generation's origin.
This represents the existing 64-beat limit, including terminal events at beat
64, with much finer resolution than output scheduling requires. Round once
when recording/importing into this representation. Use wider intermediates for
conversion and the existing absolute clock for scheduling; never accumulate
rounded step durations. Keep the current 3840-PPQN SMF export convention.

Message lengths are derived from status bytes during forward traversal. Preserve
stable event order at equal times. Do not add a permanent general-purpose
32-byte event index or per-event source/output identifiers. Capture-time source
pairing remains temporary state; playback uses the loop's configured outputs.

Preserve fine-CC and NRPN/RPN atomic groups. Equal timestamps alone must not
define a group: unrelated events can share a timestamp. Use compact group
boundary metadata only where required, and include its actual cost in reports.
Keep the existing within-group byte order and output lock semantics.

The event payload upper bound is `7 * event_count` bytes:

| Events | MIDI bytes, at most | Timestamps | Payload total |
| --- | ---: | ---: | ---: |
| 24, such as twelve completed notes | 72 B | 96 B | 168 B |
| 512 | 1,536 B | 2,048 B | 3,584 B |

These totals exclude headers, capacity slack, transaction metadata, decoder
state, active-note accounting and temporary editing storage. Do not describe
them as total runtime memory. One-byte and two-byte messages use less payload.

## Allocation, capture and editing

Keep `events` as the existing maximum, including terminal releases and expanded
parameter messages. Do not allocate that maximum on arm. Start with a small
allocation sufficient for a common short phrase and grow in bounded chunks
through the checked allocator. Release excess capacity after commit when safe.
Report both logical bytes and actual allocated bytes, including growth peaks.

Append captured messages directly to the compact candidate. Remove the live
looper's dependency on the SMF recorder queue and prepared file-player index.
Retain SMF parsing/writing for explicit Load/Save and ordinary file operations;
do not invent another disk format or remove independent SD recording support.

Before accepting a captured Note On or held-pedal activation, ensure the
candidate can represent its terminal release. No partially appended MIDI/time
pair or transaction may become visible. On allocation or capacity failure,
discard the candidate, retain the committed generation and report the error.
Release candidate capture state without disturbing already accepted output
notes from live monitoring.

Playback must not allocate an event-count-sized queue. Read directly from the
committed generation with small cursors per selected destination. Prepare
required output accounting before launching a committed generation, using its
actual outstanding-note and distinct-control requirements, not three separate
event-count-sized worst cases. An accepted Note On must always have a cleanup
record available before it enters the existing output queue.

Retain active, previous and candidate generations only when the existing
Undo/edit contract needs them. Overdub performs a stable merge directly into a
compact candidate. Reverse preparation may use a temporary compact offset or
pairing workspace; release it when preparation finishes. Backward playback must
use a bounded cursor/index strategy rather than rescanning the entire phrase
for every event. Measure and account for any retained direction index explicitly.
Do not rebuild a full SMF or full 32-byte index for normal playback.

Keep note durations, velocities, pedal transitions and group order correct in
both reverse operations. Temporary note pairing and capture records are not
duplicate output ledgers: they describe recorded input or editing relationships,
not messages accepted by a destination. Keep them compact and scoped to the job.

## One output note ledger

Replace the suppressing ownership implementation with compact shared accounting.
Each outstanding-note record identifies the source, output connection lifetime,
channel and pitch, plus an unsigned count. Repeated Note Ons increment the count;
matching Note Offs decrement it. Note On with velocity zero is a Note Off.
Retain source identities until their queued traffic and cleanup are finished.

Use compact contiguous storage or existing suitable pooled storage, not a
separately allocated heavyweight linked-list node for every small claim. Start
small and grow with actual concurrent claims. Do not cap notes at ten: repeated
attacks, sequencers, sustain and multiple sources can exceed ten fingers.
Choose count width with checked overflow handling; never saturate silently.

At the existing output acceptance point:

1. Prepare accounting storage before accepting a new outstanding note.
2. Forward the message through the existing queue without cross-source
   suppression, deduplication or replacement.
3. Change the ledger only when the message is accepted. Failed or retried sends
   must not increment or decrement it twice.

Forward orphan releases too, but do not create negative counts. Each source's
counter is independent: a release from source B must not decrement source A.
The ledger represents accepted messages, not a prediction of sounding voices.
Preserve output ordering and delivery/retirement guarantees; do not move note
accounting to an unrelated callback or introduce another output path.

On Stop, drain the source's remaining counts through matching Note Offs, one per
outstanding Note On. On output backpressure, retain cleanup and retry without
double release. A disconnected destination's obligations must never be sent
to its replacement connection. Panic and explicit channel reset/all-notes-off
operations must reconcile the ledger with their existing documented scope.

Held pedals in CC 64..69 use a per-source active state, not a Note On counter.
Repeated down values do not require multiple pedal-up messages. Ordinary pedal
messages and source cleanup are forwarded even when another source holds that
pedal. Document that channel-wide pedal cleanup can affect other playing.

## Remove duplicate ledgers and migrate consumers

`midi_state.h` retains latest known destination controller, bend, program and
pressure values for pickup and other state consumers. It must not maintain a
second authoritative active-note switch alongside counted source accounting.
Separate per-key pressure values from active-note existence where necessary;
pressure data is not another note counter.

Migrate the file player's output `sounding_` and pedal cleanup records to the
shared source ledger. Keep input/index-building note-pairing records where
needed. Migrate arpeggiator output ownership and all existing live, routed,
legacy-forwarded and generated submissions to the same accounting boundary.
Preserve source information until that boundary. Routing tokens for delayed
releases and input-capture pairing remain distinct responsibilities.

Check every consumer of global note state, including key-pressure pickup,
activity/idle checks, Panic, diagnostics, file-player cancellation and emulator
shutdown. Derive active-note queries from the counted ledger. Remove obsolete
ownership credits, physical-versus-logical counters, suppression flags and
redundant cleanup collections completely. Do not retain two implementations.
Any target-specific changes belong under the target's existing directory.

## Global continuous-control cleanup policy

Add one setup-wide setting, `loop_stop_reset_controls: bool = False`, through
the existing model, compiler, editable export and runtime configuration paths.
It applies to loop-stop cleanup, without per-loop or per-patch overrides.

Notes and held pedals always receive their required releases. With the setting
false, leave continuous values unchanged. With it true, reset only continuous
parameters actually accepted from that loop on each destination. Include bend,
channel/key pressure, CC and NRPN/RPN as applicable; exclude program and bank
selection. Do not restore pre-loop history or arbitrate between competing
sources. Reset messages are deliberate output writes and update global pickup
targets normally. Explain that they can override another source on that channel.

The agreed neutral-value policy is:

- Pitch bend resets to center, raw MIDI value 8192 (signed bend zero).
- Channel pressure and polyphonic key pressure reset to zero.
- Other CC, NRPN and RPN parameters reset only to explicitly configured neutral
  values. Parameters without a configured neutral value remain unchanged.

Carry neutral-value declarations through the existing parameter-address and
configuration machinery, distinguishing destination, channel, parameter kind
and address. Keep them setup-wide alongside the stop policy. There is no
universal zero value for arbitrary CC/NRPN/RPN parameters; do not substitute
zero, restore historical values or send broad Reset All Controllers messages.
Track touched controls compactly or derive them from the committed phrase;
do not allocate a full controller universe per loop or treat unsent events as
touched. Queued writes and partial playback must be covered.

## Implementation sequence

1. Characterize current emitted messages, memory charges and state consumers.
   Record the same small and full phrases used for final comparison.
2. Replace output ownership/accounting and migrate consumers. Update tests
   which currently require cross-source suppression to require transparent
   balanced forwarding instead. Preserve pickup and queue acceptance behaviour.
3. Replace live generation storage, capture and forward scheduling. Remove the
   looper's recorder queue/index dependencies and blanket arm reservations.
4. Adapt overdub, Undo, reverse editing, backward traversal and SMF conversion.
   Keep generation replacement transactional on failure.
5. Add the global stop policy, update compiler resource estimates and user
   documentation, and remove superseded storage/accounting code.
6. Measure complete memory and timing results, including temporary peaks, before
   claiming the replacement meets the intended savings.

## Verification and acceptance

Extend existing focused tests rather than creating parallel suites. Assert
emitted message sequences and measured allocation charges, not container layout.

- Repeated same-pitch attacks, interleaved sources, orphan releases, zero-velocity
  Note Ons and stopping one source while another remains active. Every accepted
  recorded Note On and Note Off passes through; cleanup matches remaining counts.
- Repeated pedal downs/ups, both global continuous-control policies, partial
  playback, blocked outputs, retries, disconnection/replacement and Panic.
- Empty/silent loops, 24-event and 512-event phrases, mixed message lengths,
  identical timestamps, maximum duration, tempo changes and long-run phase.
- Reverse pairing, overlapping same-pitch notes, held-pedal intervals, fine-CC
  and NRPN/RPN groups, Undo, overdub and SMF round trips.
- Allocation failure during growth, terminal release, playback admission,
  editing and cleanup. The old take survives failed candidates and no accepted
  onset loses its cleanup obligation. Exercise more than ten simultaneous notes.
- Pickup, idle/activity queries, file and arpeggiator cancellation, and emulator
  shutdown use the unified ledger correctly.

Report payload, capacity slack, allocator overhead, capture/decoder state,
shared note accounting, destination state, generations and edit/growth peaks
separately. Compare 24 and 512 events, one and multiple outputs, note-heavy and
controller-heavy phrases, and short phrases with high simultaneous polyphony.
Do not multiply shared global allocations by the number of loop views.

Acceptance requires the 512 three-byte-message payload to be 3,584 bytes and the
24-message payload to be 168 bytes, with every additional persistent allocation
explained. Arming and short recordings must no longer reserve memory in
proportion to an unused maximum capacity. Establish total-memory regression
budgets from the implemented compact representation, rather than advertising
the payload as the entire runtime cost.

Use the existing normal/diagnostic build and memory-budget checks. Record
target-specific results under the target directory. Host tests do not establish
physical jitter, SD service latency or a receiver's voice-assignment behaviour;
retain the physical checks in `plan/looper.md`.

## Additional work beyond the prompt

None.
