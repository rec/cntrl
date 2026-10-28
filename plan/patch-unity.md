# Patch unity

Status: implementation started. The existing setup and song selection models
are renamed ProgramChange. Unified TOML authoring, Patch execution, switch
handling and durable composite continuation are not implemented yet. The schema
below remains a proposal until those paths are replaced together.

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

## Start with one button

Every executable value is a table with a required `type`. The table has the same
shape whether it appears in a named definition, directly in a switch assignment,
in a song, or inside another patch. Do not accept strings as implicit references,
bare lists as implicit sequences, or infer a type from whichever fields exist.

Keep the common case short. A single program selection needs no named helper,
sequence wrapper or message-byte knowledge:

```toml
[switches.clean]
button = 'A'
on = { type = 'program-change', instrument = 'piano', program = 1 }
```

This uses the existing instrument declaration to resolve output and channel.
`mode` defaults to `press`, which invokes `on` once for each new press.

## One Patch, several kinds

`type` says what the patch does. These are variants of Patch, not separate
command or action systems. Common MIDI operations have readable fields:

| `type` | Content | Meaning |
| --- | --- | --- |
| `program-change` | `instrument`, `program`; optional `bank`, `volume`, `pan` | Existing ProgramChange selection |
| `control-change` | `output`, `channel`, `controller`, `value` | Send a CC |
| `pitch-bend` | `output`, `channel`, `value` | Send pitch bend |
| `nrpn` | `output`, `channel`, `parameter`, `value` | Send an NRPN selection and value |
| `sysex` | `output`, `data` | Send a SysEx payload |
| `midi` | `output`, `bytes` | Advanced use: send another complete MIDI message |
| `state` | `operation` and its documented fields | Change application state |
| `stock-firmware` | `exit_buttons` | Visit stock firmware |
| `sequence` | `patches` | Run patches in order from one activation |
| `reference` | `patch`; optional `document` | Use a named patch |

The normal authoring form for a known MIDI operation is its readable variant.
`midi` is an explicit low-level escape hatch, not a second recommended syntax for
ordinary controls. All variants compile through shared MIDI encoding and Patch
execution. Adding readable fields must not recreate a separate ControlCommand.

Channels are 1..16. CC numbers and values are 0..127. Pitch bend is -8192..8191,
with 0 at the center. NRPN parameters and values are 0..16383. ProgramChange
retains the current 1..128 numbering and named program lookup. These are musical
values; authors do not split them into MIDI bytes or subtract one from channels.

Examples below are independent fragments. Instrument aliases such as `piano`
refer to existing instrument declarations. Output names must exist on the target.

## Named patches and composition

Name a patch only when it needs reuse or a place in the patch browser. Inline
patches remain sufficient for a simple switch assignment. Reusable definitions
live in a dictionary named `patches`:

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

A definition's table key is its stable identity. References use that key, never
a display label or an instrument/program match. Labels default to the key for
named patches and to a useful description for inline patches. Existing `name`,
`display_name`, `style` and category/style tags remain optional presentation
metadata; authors need none of them to execute a patch. Sequence children do not
replace the parent patch's performance display while they run. A reference uses
the referenced presentation unless explicitly overridden at that occurrence.

A sequence can contain inline patches, references and other sequences. It uses
normal Patch values throughout. There is no separate step, action or command
schema. Order is significant; a sequence sends both selections on one activation.

The existing ProgramChange bank resolution and message order stay intact:
bank MSB, bank LSB, program, optional volume, optional pan. A standalone `midi`
patch does not require a program selection or an instrument alias.

## MIDI without byte arithmetic

```toml
[switches.sustain]
button = 'A'
mode = 'momentary'
on = { type = 'control-change', output = 'din', channel = 1, controller = 64, value = 127 }
off = { type = 'control-change', output = 'din', channel = 1, controller = 64, value = 0 }

[patches.bend-up]
type = 'pitch-bend'
output = 'usb-device'
channel = 1
value = 4096

[patches.filter]
type = 'nrpn'
output = 'din'
channel = 1
parameter = 130
value = 388

[patches.device-message]
type = 'sysex'
output = 'usb-host'
data = [0x7D, 1, 2] # Example noncommercial payload; F0/F7 are added.
```

NRPN expands into CC 99, CC 98, CC 6 and CC 38, in that order. Do not add an
unrequested parameter reset. Keep this group together relative to other ordinary
messages on the same output; MIDI real-time traffic may interleave. Merely
placing each CC in an independent queue would not preserve the parameter/value
relationship under competing traffic.

SysEx `data` contains payload bytes, each 0..127, without delimiters. The compiler
adds F0 and F7. Long messages stream cooperatively rather than fit inside the old
small action batch. Neither SysEx nor other MIDI sends wait for an instrument
reply unless a future, explicitly defined feature implements that behavior.

For MIDI not covered by a readable variant, use one complete message:

```toml
[patches.start]
type = 'midi'
output = 'din'
bytes = [0xFA] # MIDI Start.
```

This supports notes, system common and real-time messages as well as any other
valid MIDI message. Raw bytes include the status and any delimiters. Validate
them using the shared MIDI parser; reject incomplete messages and inherited
running status. Several messages use a sequence. Preserve authored order.

For the existing ProgramChange selection, `instrument` resolves output and
channel. Other MIDI variants specify output directly, and channel where the
message needs one. There is no hidden current output or channel to inherit from
an earlier patch. Use existing supported output IDs, including USB outputs;
this plan does not add drivers for currently unsupported hardware ports.

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
implementation. Brightness and interface changes affect the active runtime
state only; executing them does not save the setup. Persistence requires the
existing explicit Save operation. Document the effect and arguments of each
supported state operation alongside the schema, rather than leaving authors to
guess what an internal method name means.

`StockFirmware` remains its own typed Patch variant because it suspends execution
and resets the processor. It is not an opaque string passed to another module.
Keep the existing configurable return pair and release/arming behavior.

Navigation, menu selection and activation of the currently selected patch are
also explicit operations of the `state` variant in the shared executor.
`next-patch` changes the preview only; `activate-selection` executes the selected
Patch. Built-in button bindings construct Patch values internally, so users need
not author the UI or replace normal navigation to use this feature.
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
from the entire reachable patch, including referenced sequences. A momentary or latched assignment containing raw MIDI or SysEx reserves the
whole destination output until its Off cleanup completes or the user explicitly
abandons it. Other assignments cannot share that output, even on another
channel. Apply this rule through sequences and references. Readable MIDI patches
retain ownership of their identified controls. Do not treat arbitrary SysEx as
though its affected controls were known. Firmware switching and leaving the switch-assignment context require special
care: a momentary switch cannot receive its release while another firmware is
running. Initially allow those operations only in `press` assignments and song
positions, including their nested sequences. Reject them in either branch of a
momentary or latched assignment with a clear diagnostic. This restriction concerns
execution context, not a second Patch type. Complete any other assignment's
required cleanup before leaving. Do not add automatic note
ownership, inferred inverses or rollback as part of this unification.

## Optional catalogue, songs and references

Simple switch-only setups need neither a catalogue nor groups. For setups that
use the existing patch browser, a dictionary of named patches is a catalogue,
not a performance order. Replace
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

## Errors users can fix

Report all independent authoring errors together, naming the source file and
patch or switch path. Say what is wrong and what is accepted, for example:
`switches.sustain.on.channel: expected 1..16; got 0` or
`patches.both.patches[2]: patch "lead" does not exist`. Show the full reference
chain for cycles. Unknown type names and fields are errors, with no silent
fallback to a different kind of patch. Validate cross-document dependencies
before sending anything.

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
from an allowed switch event. Invalid context combinations fail at compilation.

## Additional work beyond the prompt

None.
