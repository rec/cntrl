# Momentary and latched actions

Status: implemented in document format 16. Physical switch/MIDI qualification
remains pending. This develops the idea in [features.md](features.md), using the
existing optional-module, configuration, MIDI scheduling and formatting
infrastructure. See [usage and implementation](../doc/momentary-latched.md).

## Behavior and scope

An assignment connects one physical switch to a named two-state action:

- `momentary`: press requests On; release requests Off.
- `latched`: each new press toggles the requested state; release does nothing.

Examples include sustain, effect bypass, a fixed solo boost and an alternate
sound. Each assignment explicitly supplies the commands for both states. Off
means sending its authored Off command, not guessing the receiving instrument's
previous state. A solo boost therefore uses explicit normal and boosted levels.
An alternate sound uses two explicit patch selections.

The first version supports one Control Change or one existing MIDI patch
selection per state. Both states must use the same command kind. Arbitrary
message lists, note/chord generation, SysEx, timed gestures, expression curves,
relative values and restoring an inferred previous sound are outside this feature.
Note/chord ownership belongs to its separate feature in features.md. Pedal
functions from [patch-functions.md](patch-functions.md), including firmware
switching, cannot be On or Off commands: they do not have reversible MIDI-state
semantics.

## Configuration

Add an optional `actions` dictionary to a performance setup. Dictionary keys are
stable assignment identities within that setup. An absent or empty dictionary
means no configured actions. There is one authoring representation:

```toml
[actions.sustain]
name = 'Sustain'
button = 'A'
mode = 'momentary'
on = { instrument = 'piano', controller = 64, value = 127 }
off = { instrument = 'piano', controller = 64, value = 0 }

[actions.solo]
name = 'Solo boost'
button = 'B'
mode = 'latched'
on = { instrument = 'sax', controller = 7, value = 110 }
off = { instrument = 'sax', controller = 7, value = 85 }

[actions.alternate]
name = 'Alternate sound'
button = 'C'
mode = 'momentary'
on = { patch = 'lead-sound' }
off = { patch = 'clean-sound' }
```

Instrument keys refer to existing setup instruments and inherit their output and
channel. Patch keys refer to existing named MIDI patches in the same setup.
Patch selections resolve at compilation and use the existing bank/program/volume/
pan ordering. There is no second inline patch representation in an action.

Each assignment requires `button`, `mode`, `on` and `off`. `name` defaults to the
assignment key. Optional `style` uses the existing Style model and cascade; it
may include an icon reference under the existing deferred-icon rules. No font
size, hold duration, debounce time or initial-On option is introduced.

Validation must reject:

- Unknown properties, modes, instruments, patches or unsupported outputs.
- Empty/invalid names and identities using existing configuration text limits.
- Controller/value outside 0..127, non-integers and boolean-as-integer values.
- Mixed CC and patch states, or definitions mixing both command shapes.
- Two assignments on the same button, or assignment to a reserved/absent button.
- On and Off commands targeting different output/channel pairs.
- Two assignments owning the same CC on the same output/channel, or two patch
  assignments owning the same output/channel.
- A CC assignment overlapping a patch assignment's authored controller messages
  on the same output/channel, including bank select, volume and pan.

Conflicts are checked after resolving instrument aliases to actual output/channel
pairs. This prevents two independent Off transitions from fighting over the same
resource. Do not infer undocumented controller effects inside a receiving device.

Numeric MIDI values are dimensionless. Any future configuration values with
physical units must use `reccy.configuration.units` like the existing models.

## Module and controls

Provide an optional Actions module using the existing explicit module menu and
build inclusion pattern. Its implementation owns assignment state and receives
button edges through the core; it does not read physical GPIO or MIDI ports.

The target declares the available action switches and a reserved Back switch.
On the existing six-switch arrangement, A through E are assignable and F is Back.
No action-page chords are recognized. There is one bank of assignments in this
version; no hidden paging or dynamic reassignment while held. Unassigned buttons
do nothing. The compiler checks the target's capacity rather than silently
truncating assignments.

Entering Actions starts every assignment locally Off and sends no MIDI. Switches
already held at entry are disarmed individually until released. This avoids an
inherited press activating an action. The page reports local command state, so
initial Off must not be described as a confirmed remote device state.

The module provides temporary access to these assignments. Leaving it resets
active actions through the Off process below. Running latched actions invisibly
behind other modules is outside this version. Existing patch-browser navigation,
recall, undo and chord behavior continues outside Actions.

## Input and state machine

Use the existing debounced press/release events. Do not add an artificial hold
threshold, chord window or a second debounce filter. Dispatch accepted presses
at the next normal cooperative service opportunity.

Each assignment stores the physical held flag, requested state, last successfully
accepted command state, pending transition and any error. These are RAM state,
not persistent configuration. Duplicate press or release events are ignored.

| Event | Momentary | Latched |
| --- | --- | --- |
| New press | Request On | Toggle requested state |
| Release | Request Off | Keep requested state |
| Duplicate edge | No change | No change |
| Exit or cancellation | Request Off | Request Off |

Treat an accepted MIDI batch as indivisible according to the existing output
scheduler contract. The state change becomes locally accepted only after the
whole batch has been accepted. It does not mean the device received or acted on
it. Resolve output ownership and cancellation with the scheduler, not by writing
around its queues.

If On has not been accepted when a momentary switch is released, cancel that
pending On. Send no Off for an On that was never accepted. If On was accepted,
Off must follow it on the same route even if the release occurs before On has
left the transmitter. Never invert the order or discard that required Off.

For latched presses arriving under backpressure, toggle the requested state.
Coalesce requests that have not been accepted; do not build an unbounded event
queue. Preserve every necessary Off following an accepted On. After an Off is
accepted, a still-requested On may be scheduled again. Tests must distinguish
pending requests from batches already accepted by the shared scheduler.

## MIDI scheduling and failures

Extend the existing shared scheduler only as needed to reserve cleanup capacity
and track action-owned batches. Do not create a parallel MIDI queue or parser.
Before accepting On, ensure a bounded path remains available to accept its Off
later. Memory requirements must be bounded by installed assignment capacity,
never by press count or duration. Round-robin service must prevent one blocked
output from starving other assignments, UI work or MIDI reception.

A temporarily full queue keeps the request pending and displays Pending. A
missing output rejects a new On and displays Unavailable. Errors do not change
an Off assignment to On. Never report an action as safely Off merely because its
Off command failed to enqueue or transmit.

If the destination disconnects after accepting On, discard unsent On work for
that destination, record an unresolved cleanup and display Unknown/Unavailable.
Do not replay On or its cleanup automatically onto a newly attached USB device,
which may be a different instrument. Permit leaving with a visible warning that
remote state could not be reset. Reconnection requires a new explicit press;
a held momentary switch must be released before rearming. A subsequent explicit
action may send the authored Off command first to establish its baseline.

There is no acknowledgement in ordinary MIDI for CC/program delivery. Document
that unplugging the cable or losing power may leave the instrument in its last
state and require a manual reset. Do not claim exactly-once delivery or physical
state confirmation.

## Exit, replacement and Panic

Back begins a cooperative cleanup operation:

1. Stop accepting new On requests and suppress held switches until release.
2. Cancel On requests that have not been accepted.
3. Request each necessary Off, at most once per accepted On state.
4. Wait for action-owned output work to drain on available destinations, then
   return to the module menu. Report disconnected/failed destinations explicitly.

Expose a Closing/Pending state while cleanup is blocked. An available but
backpressured output must not silently lose its Off; keep servicing inputs and
provide a second explicit Back action to abandon cleanup with an Unknown warning.
Abandonment drops unsent action-owned work and must not cause it to replay later.

Use the same cleanup contract before changing setup, replacing the active setup
through upload, or initiating a firmware-switch function. Reject/defer the
replacement until cleanup is complete or explicitly abandoned. Keep the old
resolved commands and their backing document alive until cleanup finishes;
never retain pointers into staging or a replaced active document. Copying each
small resolved On/Off command into fixed runtime storage is acceptable.

Panic has priority. Cancel new action activation, perform action cleanup on
available outputs, then run Panic. If an output is unavailable, Panic must still
run on the others. Reset local latch state and disarm held switches until release.
Panic's Reset All Controllers does not replace authored Off commands: an effect
bypass or program selection may need an explicit reset. After Panic, action
status represents the reset/cancellation outcome, including Unknown failures.

Power-on and reset always start locally Off and send no action MIDI. Do not
persist live latch states or automatically resend them after reboot. Selection
of a new setup also starts locally Off. Configuration persists normally through
setup save, export and revision history.

## Feedback and formatting

Display the assignment name, its bound switch and an explicit state. At minimum
support Off, On, Pending and Unknown/Unavailable. A held momentary action and a
latched On action both visibly identify the active assignment. Mode must be
available in the assignment information so a user can distinguish their behavior.

Reuse existing page/field and style machinery. Expose uniquely named action fields
such as `action.name`, `action.mode` and `action.state`; do not introduce a second
styling language or overload patch-specific fields. Text must make state clear
without depending solely on color, an icon or backlight changes. Repaint only
when visible state changes. Exact screen geometry is deliberately outside this
specification and the separate layout work.

## Persistence, compilation and integration

Add typed configuration models for assignments and the two command shapes.
Compile references into compact resolved commands, preserving authored references
and styles in export metadata. Extend firmware decoding with explicit record
lengths, indices, counts and message validation; malformed input must fail before
any assignment becomes active. Version the compiled format and update all
producers/consumers together using the project's current format policy.

Existing setups with no actions retain their behavior. A setup requiring Actions
must fail clearly when compiled for a target/build without the module. Reuse
existing setup transfer and two-slot persistence. Downloads, pedal-independent
edits and history restore must round-trip the action configuration. Runtime
pending/error/latch state must never enter exported configuration or history.

This requires a small, explicit extension to module input dispatch and output
ownership. Keep state-machine logic portable and independently testable, with
target-specific bindings under the appropriate maker directory. Do not introduce
a general scripting engine, arbitrary event bus or runtime plugin loader.

## Implementation sequence

1. Define assignment models, validation and resolved command records. Add focused
   configuration round-trip tests, including conflicts after alias resolution.
2. Extend compiled encoding/decoding and history/export support. Test malformed
   records before integrating device execution.
3. Implement the independent state machine and scheduler cleanup reservation,
   cancellation and completion boundaries. Resolve the known MIDI batch and
   disconnect audit issues before relying on these contracts.
4. Add Actions module registration, button routing and state fields using existing
   formatting. Integrate cleanup with setup replacement, module exit and Panic.
5. Build with the module included and excluded, check RAM/flash budgets, then
   flash through the upload helper under the normal project workflow. Qualify
   physical behavior separately from native tests.

## Acceptance tests

Use deterministic fake time, button events and a controllable MIDI sink for the
logic tests. Assert messages and visible state rather than private fields.

- Momentary press sends On once; release sends Off once. Holding produces no
  repeating messages; duplicated edges produce no additional commands.
- Two latched press/release cycles send On then Off, with no release messages.
- Short taps, rapid toggles and release-before-queue-acceptance preserve the
  specified cancellation and ordering semantics.
- Two held assignments operate independently. One blocked destination does not
  starve another, and sustained input does not grow allocated storage.
- A full ordinary MIDI queue cannot consume the reserved cleanup capacity.
- Entry with a held switch, exit while held and returning to the module never
  cause an inherited press or an orphaned release to activate an action.
- Exit drains accepted On followed by Off, supports explicit abandonment under
  backpressure and never replays abandoned commands.
- Disconnect between On acceptance and completion produces Unknown, allows
  unrelated cleanup/Panic, and sends nothing automatically on reconnection.
- Setup upload/replacement and firmware switching cannot invalidate commands or
  their backing memory before cleanup completes.
- Panic cancels active/pending assignments; held switches remain disarmed until
  release and no old On batch executes after Panic.
- Boot and entering the page send no MIDI; live latch state is never restored.
- CC states and alternate patches send exactly their resolved commands in the
  existing bank/program/controller order. Off uses its explicit authored value.
- Configuration export/recompile and history restore preserve modes, bindings,
  command references and style properties; unknown/invalid records fail safely.

On hardware, capture MIDI for sustain, latched bypass, solo boost and alternate
patches. Verify fast taps, simultaneous switches, held exit, saturated outputs,
disconnection, Panic and reboot. Record event-to-message latency and maximum
button-service gap under load. Confirm state text reflects accepted/pending/error
outcomes without claiming remote acknowledgement.

## Additional work beyond the prompt

None. This document specifies the requested feature; implementation, layout
changes and additional action types remain separate work.
