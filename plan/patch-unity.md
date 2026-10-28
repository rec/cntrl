# Patch unity

Status: proposed. This document specifies an architectural replacement, not an
implemented format or a compatibility layer.

## Goal

A Patch is the only executable value a button can invoke. It can select an
instrument program, send any MIDI message to any supported MIDI output, change
application state, switch to stock firmware, or execute an ordered list of other
patches. These share one representation and one execution path.

Rename the existing MIDI-selection `Patch` structure to `ProgramChange`. Preserve
its instrument, program, bank, optional volume/pan and lookup behavior. It is a
high-level selection that can emit several MIDI messages, not just the single
MIDI Program Change wire message.

`SwitchAssignment` describes when to invoke patches. It is not another kind of
executable content. Remove the separate `ControlCommand`, `PatchCommand` and
`FunctionPatch` authoring models. MIDI Control Change remains a MIDI message
kind, not a competing top-level executable concept. The unrelated history CLI
`Command` enum stays unchanged.

## One TOML rule

Every executable value is a table with a required `type`. The table has the same
shape whether it appears in a named definition, directly in a switch assignment,
in a song, or inside another patch. Do not accept strings as implicit references,
bare lists as implicit sequences, or infer a type from whichever fields exist.

The initial types are:

| `type` | Required content | Meaning |
| --- | --- | --- |
| `program-change` | `instrument`, `program` | The existing selection structure, renamed `ProgramChange` |
| `stock-firmware` | `exit_buttons` | Enter stock firmware, retaining the existing return mechanism |
| `midi` | `output`, `bytes` | Send one complete MIDI message |
| `state` | `operation` and its required fields | Apply one supported, typed application-state change |
| `sequence` | `patches` | Execute a nonempty ordered list of Patch values |
| `reference` | `patch` | Invoke a named patch; optional `document` selects another setup |

Optional presentation properties belong to Patch: `name`, `display_name`,
`style` and existing category/style tags. They do not change execution. A named
patch's table key is its stable identity; its display name can change without
breaking references. A reference's presentation overrides apply to that use,
without modifying the referenced definition.

Keep the common case short. A single program selection needs no named helper,
sequence wrapper or message-byte knowledge:

```toml
[switches.clean]
button = 'A'
on = { type = 'program-change', instrument = 'piano', program = 1 }
```

This uses the existing instrument declaration to resolve output and channel.
`mode` defaults to `press`, which invokes `on` once for each new press.

## Named patches and composition

Reusable definitions live in a dictionary named `patches`:

```toml
[patches.clean]
type = 'program-change'
instrument = 'piano'
program = 1
name = 'Clean piano'

[patches.lead]
type = 'program-change'
instrument = 'synth'
program = 12
bank = 2
volume = 110
pan = 0

[patches.both]
type = 'sequence'
name = 'Piano and lead'
patches = [
    { type = 'reference', patch = 'clean' },
    { type = 'reference', patch = 'lead' },
]

[switches.both]
button = 'B'
on = { type = 'reference', patch = 'both' }
```

A sequence can contain inline patches, references and other sequences. It uses
normal Patch values throughout. There is no separate step, action or command
schema. Order is significant; a sequence sends both selections on one activation.

The existing ProgramChange bank resolution and message order stay intact:
bank MSB, bank LSB, program, optional volume, optional pan. A standalone `midi`
patch does not require a program selection or an instrument alias.

## Arbitrary MIDI

Use `output` and a byte array for the universal MIDI case. This is the single
MIDI-message representation; do not introduce a separate CC-only command model.
TOML decimal and hexadecimal integers are both valid. Comments can describe
manufacturer-specific bytes without becoming data.

```toml
[patches.sustain-on]
type = 'midi'
output = 'din'
bytes = [0xB0, 64, 127] # Channel 1, CC 64, value 127.

[patches.sustain-off]
type = 'midi'
output = 'din'
bytes = [0xB0, 64, 0]

[patches.bend-up]
type = 'midi'
output = 'usb-device'
bytes = [0xE0, 0, 96] # Channel 1, pitch bend +4096.

[patches.device-message]
type = 'midi'
output = 'usb-host'
bytes = [0xF0, 0x7D, 1, 2, 0xF7] # Example noncommercial SysEx.
```

An NRPN is an ordinary sequence of MIDI patches, not a new executable type:

```toml
[patches.nrpn]
type = 'sequence'
patches = [
    { type = 'midi', output = 'din', bytes = [0xB0, 99, 1] },
    { type = 'midi', output = 'din', bytes = [0xB0, 98, 2] },
    { type = 'midi', output = 'din', bytes = [0xB0, 6, 3] },
    { type = 'midi', output = 'din', bytes = [0xB0, 38, 4] },
]
```

The above selects NRPN 130 and sets its 14-bit value to 388 on channel 1.
Notes, arbitrary CCs, pitch bend, system common, real-time messages and SysEx all
use `midi`. Each `bytes` array contains exactly one complete message, including
its status and any SysEx delimiters. Use a sequence for several messages. Do not
rely on running status inherited from another patch. Preserve every authored
message and its order; do not silently deduplicate controllers or add resets.

Validate messages using shared MIDI logic. Outputs are the target's existing
output IDs, including supported USB outputs. Do not invent new Omniport drivers
or imply that every catalogued connector is an implemented output. Large SysEx
must stream cooperatively through the existing scheduler rather than fit inside
the current small action-batch record.

## Application state and stock firmware

Application-state patches use a closed set of typed operations. Each operation
has explicit fields and existing validation. For example:

```toml
[patches.stage-brightness]
type = 'state'
operation = 'brightness'
value = 192

[patches.interface-on]
type = 'state'
operation = 'usb-midi-interface'
value = true

[patches.next]
type = 'state'
operation = 'next-patch'

[patches.original]
type = 'stock-firmware'
exit_buttons = 'A+F'
```

These examples reuse existing capabilities. Do not expose arbitrary attribute
paths, Python expressions, strings to evaluate, or a generic dictionary of
arguments. Adding a supported state operation requires an explicit typed
implementation. Each operation defines whether it changes a runtime value or a
saved setup; executing a patch must never silently save unrelated edits.

`StockFirmware` remains its own typed Patch variant because it suspends execution
and resets the processor. It is not an opaque string passed to another module.
Keep the existing configurable return pair and release/arming behavior.

Navigation, menu selection and activation of the currently selected patch are
also explicit operations of the `state` variant in the shared executor. Built-in button
bindings construct Patch values internally, so users need not author the UI.
Hardware scanning reports switch edges; it does not execute a second family of
button callbacks. Background MIDI processing and scheduler ticks remain normal
runtime work, not synthetic button activations.

## Switch timing

Use `switches` for `SwitchAssignment` configuration, replacing `actions`.
Assignments contain Patch values only:

```toml
[switches.sustain]
button = 'A'
mode = 'momentary'
on = { type = 'reference', patch = 'sustain-on' }
off = { type = 'reference', patch = 'sustain-off' }

[switches.alternate]
button = 'B'
mode = 'latched'
on = { type = 'reference', patch = 'lead' }
off = { type = 'reference', patch = 'clean' }
```

- `press`: requires `on`, forbids `off`; each press invokes `on` once.
- `momentary`: requires `on` and `off`; press requests On, release requests Off.
- `latched`: requires `on` and `off`; successive presses alternate states.

All three invoke the same Patch executor. On and Off need not have the same
Patch type, output or channel. Those old restrictions came from the two-command
representation, not from switch timing. Off always means its explicitly authored
patch; it does not mean reversing a previous patch or guessing remote state.

Preserve accepted-work completion, required Off cleanup, entry-switch suppression,
output backpressure and explicit abandonment behavior. Resolve resource ownership
from the entire reachable patch, including referenced sequences. Give a specific
error when an existing ownership guarantee cannot be met; do not treat arbitrary
SysEx as though its affected controls were known. A firmware switch or UI-context
change must complete required cleanup before leaving. Do not add automatic note
ownership, inferred inverses or rollback as part of this unification.

## Navigation, songs and references

A dictionary of named patches is a catalogue, not a performance order. Replace
the old category-to-patches dictionary with `groups`, whose ordered entries are
Patch values. For example:

```toml
[groups]
Live = [
    { type = 'reference', patch = 'clean' },
    { type = 'reference', patch = 'both' },
    { type = 'reference', patch = 'original' },
]
```

Song patch lists use these same Patch values, with existing persistent occurrence
identities retained. A song list is a sequence of performance positions, advanced
by the performer. A `sequence` Patch explicitly executes all of its children
from one activation. Do not confuse the two or execute a whole song on selection.

External references use the same reference variant:

```toml
[patches.borrowed]
type = 'reference'
document = 'other-setup'
patch = 'clean'
```

`document` identifies an existing setup, as in current song references. Resolve
`patch` by its stable definition key, not by display name or instrument/program
matching. Preserve the existing host companion-document and SD reference workflows.
Validate dependencies before activation, and keep the resolved source data alive
for the entire invocation. Edits cannot replace data beneath pending execution.
Detect missing references and cycles across documents as well as within one.

## Execution and continuation

One core executor dispatches Patch variants. Existing optional modules expose
operations through the core rather than calling each other directly. Reuse MIDI
encoding, output scheduling, state setters and firmware-switching machinery.

Sequence children run in order. A child completes before the next starts;
backpressure means pending, not permission to skip or replay it. Preserve MIDI
message boundaries while allowing required real-time traffic. Multiple outputs
are ordered, not simultaneous or transactionally atomic. On failure, stop at the
failing child, report it, and do not run later children. Earlier messages and
state changes may already have taken effect; never claim they were rolled back.
Distinguish Pending, Completed, Failed and Unknown remote state in existing UI
feedback. A locally completed MIDI send is not an instrument acknowledgement.

Do not generalize the existing Undo history into an automatic inverse for all
patches. Preserve restoration of known previous program selections, but do not
replay a composite containing firmware switches or other side effects as an
implicit undo. Record partial execution honestly; switch Off patches remain
explicit, separately authored content.

A StockFirmware child suspends the current invocation. Extend the existing durable
visit continuation to identify the enclosing patch, nested child positions,
source revisions and song/setlist occurrence when present. After an explicit
stock return, validate and consume the continuation before resuming the next
child. Finish that invocation before applying the existing song-next behavior.
Do not both resume a sequence and independently advance its song twice.
Outside a song, resume the sequence and then return to its caller. Preserve the
current no-replay behavior after ordinary boot, invalid continuation or changed
source data. As today, consumption before dispatch favors avoiding duplicate
execution; power loss can leave an operation unexecuted or its result unknown.

## Implementation and acceptance

1. Define the tagged Patch union and the single TOML schema. Rename the existing
   selection model to ProgramChange and share presentation metadata. Keep the
   existing program-table lookup support; catalogue entries are definitions,
   not another button-executable type.
2. Replace the old patch/function/action-command compilation paths with one
   resolver. Resolve stable references, collect all validation errors, preserve
   execution order and enforce actual document/RAM/output capabilities. Add no
   arbitrary patch-count or nesting limits merely to simplify storage.
3. Introduce a compiled Patch representation supporting variable-length MIDI,
   child lists and reference identities. Update firmware decoding, SD song
   records, editable exports, font text collection and transfers together. Bump
   the affected binary formats. Do not keep parallel old and new execution paths
   or silently reinterpret existing files; document the required authoring changes.
4. Route switch assignments, existing UI bindings, patch browsing and song
   activation through the same executor. Preserve the distinction between
   browsing, pressing Send and executing a composite patch. Adapt ownership and
   cleanup to composed patches, then extend durable stock-firmware continuation.
5. Add focused tests for every variant, nested and external references, cycles,
   message validation, ordering across outputs, pending/failure behavior, switch
   modes, export/recompile equality and stock-return continuation. Build the
   target firmware. Qualify MIDI bytes and switch behavior separately on hardware;
   automated tests do not establish physical behavior.

Acceptance includes a simple inline program selection, a CC-only patch, pitch
bend, SysEx longer than the old action buffer, an NRPN sequence, a multi-instrument
sequence, mixed MIDI/state changes and a nested stock visit. The same patch must
behave identically whether invoked directly, through a reference, from a song or
from either edge of a switch assignment.

## Additional work beyond the prompt

None.
