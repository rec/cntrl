# Patches, switches and songs

`patch_cycles` maps names to ordered lists of patches. A tag classifies a program
or patch independently of those lists. Items use a `tags` list; the setup's
optional `[tags.NAME.style]` tables define styles for selected tags. Tags do not
need a declaration to be used.

A fully specified Patch is an executable table with a required `type`. The same table works in a
named definition, a browser patch cycle, a switch assignment, a song position or a
sequence. A sequence executes all its children on one activation. A song advances
one position at a time.

## Simple program lists

For an instrument that stays in one bank, list its program names in order:

```toml
[instruments.synth]
channel = 1
output = 'din'
programs = ['Piano', 'Flute', {}, 'Strings']

[patch_cycles]
Live = ['Piano', 'Flute', 'Strings']
```

Array positions are program numbers 1 through 128. An empty table `{}` leaves
that position unnamed without moving subsequent programs. A flat array sends
Program Change only: it does not select bank 0 or send either bank-select
controller. Select the desired bank on the instrument before using the setup.

An entry can also be a metadata table, for example
`{name = 'Flute', tags = 'woodwind,solo'}`. Tags accept a list of strings or a
string separated by commas or colons. Program numbers and instrument identities
come from the containing structure and cannot be repeated inside these entries.

In a patch cycle, a string looks up an exact program name. If a name occurs more
than once, qualify it with `{name = 'Flute', instrument = 'synth', bank = 3}`,
using whichever qualifiers are needed to select one program. These shorthand
entries can include presentation properties, volume and pan. Named patches,
switch assignments, sequences and songs continue to use explicit typed Patches.

For explicit banks, use a table whose keys are bank numbers:

```toml
[instruments.synth]
channel = 1
output = 'din'

[instruments.synth.programs]
0 = ['Piano', 'Flute']
3 = ['Lead', 'Strings']

[[definitions.setups]]
instrument = 'synth'
name = 'default'

[definitions.setups.banks.0]
lsb = 0

[definitions.setups.banks.3]
lsb = 3

[patch_cycles]
Live = ['Piano', 'Lead']
```

Explicit banks need mappings in the setup definition. `msb` is the value sent on
CC 0; `lsb` is the value sent on CC 32. Only the configured controllers are sent.
The example configures CC 32 alone. The compiler does not guess a device's bank
mapping from its name or a catalogue's bank numbers.

The instrument identity defaults to its table key, and `setup` defaults to
`'default'`. A setup with no bank mappings or initialization data needs no
definition. Routing still requires `channel` and `output`. Catalogues containing
only names and metadata can be supplied as `--table` files; routing then belongs
in the show file.

When files are combined with `+`, different bank keys merge without changing
program numbers. Equal program arrays are retained once; conflicting arrays are
errors even with conflict avoidance enabled. Flat and bank-keyed catalogues for
the same instrument cannot be mixed. Existing explicit configurations and
examples remain supported. See `examples/patch-switcher/program-changes.toml`
for a complete flat-list example.

The documented VL70-m and Patchman catalogues keep their fixed program information
in `instruments/vl70m.toml` and `instruments/patchman.toml`. Personal classifications
are in `instruments/tags.toml`. Load the relevant catalogue and the tags file as
separate `--table` arguments, or combine them with `+`. The tags file groups exact
program names under each instrument:

```toml
[instruments.patchman.tags]
trumpet = ['Trumpet', 'Maynard', 'Alpert']
sax = ['Alto Sax', 'TenorSax', 'Bari Sax']
```

Each tag applies to every matching program name in that instrument's loaded
catalogue. Missing names in a loaded catalogue are errors. Instruments whose
catalogues are not loaded are ignored, so one tags file can serve several shows.
Tag-table order determines tag order when a program belongs to multiple tags.
If a nondefault setup is needed, declare `setup` once in the instrument table.
Changing a classification does not require editing the documented catalogue.

## Several catalogues on one MIDI destination

When two instruments receive the same Program Change, describe the route once
and attach their aligned sound catalogues:

```toml
[instruments.both]
output = 'din'
channel = 1
catalogues = ['vl70-internal.toml', 'patchman-internal.toml']

[pages.patch]
fields = ['patch_cycle.name', 'names.vl70m', 'names.patchman']

[patch_cycles]
Live = [1, 2, 3, 4, 5]
```

Paths are relative to the file containing the declaration. Each catalogue must
describe one distinct instrument and one nonempty bank, with the same number of
named program slots as the other catalogues. Slots align by program number.
The catalogue bank describes the sounds; it does not request bank-selection
messages. Set the hardware to the desired bank before using this configuration.

Each activation sends one Program Change to the configured destination, regardless
of the number of attached catalogues. Numeric cycle entries require exactly one
configured destination; with several destinations, use explicit typed patches.
Do not also supply `programs` on a declaration that uses `catalogues`.

The `names.INSTRUMENT` fields expose the documented sound names independently.
They use the same fitting, spacing, styling and already-sent stippling as the
ordinary patch name. `display_name` remains available and defaults to the two
names joined with ` / `, omitting identical duplicates. Exports retain both names
inside the program metadata and are self-contained; they do not need the original
catalogue files to recompile.

Loading `--table instruments/tags.toml` applies each underlying sound's tags to the
shared program. The effective tags are their union, plus any tags on the patch.
Unselected sounds in those source instruments are ignored when only the shared
catalogue is loaded. This allows differently tagged sounds to share one action
without changing the classifications of either sound.

`examples/patch-switcher/dual-internal.toml` represents all 64 paired Internal
slots for the stock VL70-m and Patchman units on channel 1.

## Explicit patches

```toml
[instruments.piano]
instrument = 'piano'
setup = 'default'
output = 'din'
channel = 1

[[definitions.setups]]
instrument = 'piano'
name = 'default'

[patches.clean]
type = 'program-change'
instrument = 'piano'
program = 1
display_name = 'Clean piano'

[patches.intro]
type = 'sequence'
patches = [
    {type = 'state', operation = 'brightness', value = 192},
    {type = 'reference', patch = 'clean'},
    {type = 'control-change', output = 'trs', channel = 2, controller = 11, value = 100},
]

[patch_cycles]
Live = [{type = 'reference', patch = 'intro'}]

[switches.intro]
button = 'A'
on = {type = 'reference', patch = 'intro'}
```

Instrument declarations and their bank mappings retain their existing meaning.
Switch-only setups can omit instruments, definitions, named patches and patch cycles
when their patches do not use them. Named definitions have stable keys; their
dictionary order does not define the browser order. `patch_cycles` supplies that order.

## Patch types

| Type | Required fields | Optional executable fields |
| --- | --- | --- |
| `program-change` | `instrument`, `program` | `bank`, `volume`, `pan` |
| `control-change` | `output`, `channel`, `controller`, `value` | None |
| `pitch-bend` | `output`, `channel`, `value` | None |
| `nrpn` | `output`, `channel`, `parameter`, `value` | None |
| `sysex` | `output`, `data` | None |
| `midi` | `output`, `bytes` | None |
| `state` | `operation` and the fields below | None |
| `stock-firmware` | `exit_buttons`, such as `'A+F'` | None |
| `sequence` | `patches`, a list of Patch tables | None |
| `reference` | `patch`, a stable definition key | `document` |

Every type can have `name`, `display_name`, `style` and `tags` presentation
metadata. Named definitions default their label to their key. Inline patches use
a description of the operation. References inherit the target's presentation;
metadata on the reference occurrence overrides it. Children of a sequence do not
replace the parent performance display.

Channels are 1..16. Programs use 1..128 or the existing program-name regular
expression lookup. Volume, CC controllers and CC values use 0..127; pan uses
-64..63. Pitch bend uses -8192..8191, with zero centered. NRPN parameters and values
use 0..16383. Program selections send bank MSB, bank LSB, program, optional volume
and optional pan, in that order. NRPN sends CC 99, 98, 6 and 38 as one ordered patch cycle
on its output, allowing MIDI real-time traffic to interleave.

SysEx `data` contains 0..127 payload bytes, without F0/F7; the compiler adds those
delimiters. `midi.bytes` contains exactly one complete message including its
status and any delimiters. Running status from another patch is never inherited.
Long SysEx streams through the normal cooperative output scheduler. The target
must support the named output; this format adds no output drivers.

## State operations

| Operation | Fields | Effect |
| --- | --- | --- |
| `brightness` | `value`, integer 0..255 | Set active screen brightness |
| `usb-midi-interface` | `value`, Boolean | Set the active USB MIDI bridge preference |
| `next-patch`, `previous-patch` | None | Move the preview within its current patch cycle |
| `next-patch-cycle`, `previous-patch-cycle` | None | Move the browser to another patch cycle |
| `activate-selection` | None | Execute the selected Patch through the same executor |
| `undo` | None | Restore a known previous program selection, when available |
| `menu` | None | Return to the module menu after the invocation and required cleanup |
| `back` | None | Return to performance after the invocation and required cleanup |

Brightness and USB interface state are runtime settings. Executing these patches
does not write the setup. Persistent setup settings still use the existing
explicit setup editing and Save workflow. There is no arbitrary attribute,
expression or dictionary of arguments to evaluate.

## Switch timing and ownership

Switches use A through E; F remains the context's Back control. `mode` defaults to
`press`, which requires `on` and forbids `off`. `momentary` requires both and
requests On at press, Off at release. `latched` requires both and alternates on
successive presses. On and Off may use different Patch types, outputs and channels.

The full reachable graph determines ownership, including both branches and
referenced sequences. Readable MIDI reserves its known controls. Raw MIDI and
SysEx in a persistent assignment reserve their entire output through Off cleanup
or explicit abandonment. Other assignments cannot share that output. There is no
automatic note ownership or inferred inverse. Accepted work completes before the
required Off patch runs. A held entry switch must be released before it rearms.

Stock visits and context-leaving operations are allowed in press assignments and
song positions. Either branch of a momentary or latched assignment must stay in
its context. Other active assignments finish their explicit cleanup before a
press assignment leaves. See [switch controls](momentary-latched.md).

## References and song libraries

```toml
[patches.borrowed]
type = 'reference'
document = 'other-setup'
patch = 'clean'
```

External references use the setup's logical name and a named patch's stable key.
Display labels and instrument/program matching are not identities. The setup CLI
loads adjacent `other-setup.toml` companions recursively and compiles the complete
dependency graph before opening the transport. On the pedal, sources are checked
and pinned before execution. Missing references and cycles fail before effects;
pending work prevents replacement of its source data.

Song libraries have optional `instruments`, `definitions`, `patches`, `fonts` and
`tags` context using the same authoring models. Each song's `patches` list
contains ordinary Patch tables, with an optional `id` for the position's persistent
occurrence identity. A nonempty library is compiled with `--target`; companion
setups and font assets follow the existing copy/download workflow. Inline captures
retain their compiled source; live references retain their stable document/key.

## Completion, failure and stock return

Execution is ordered across outputs. Backpressure leaves the current child
pending. A failed child stops the invocation; completed effects are not rolled
back. Completed locally means delivery to the local MIDI output, without an
instrument acknowledgement. Disconnects or abandoned cleanup can leave remote
state unknown. Undo restores known program selections and does not implicitly
replay a composite or reverse state, SysEx or firmware visits.

A stock visit checkpoints the enclosing root, completed child ordinal, source
fingerprint, caller and song/setlist occurrences when present. It also preserves
the browser context and active brightness/interface settings needed to resume.
Only an explicit stock return can resume. Its durable token is consumed before
dispatch; changed sources, invalid records and ordinary boot never replay it.
Remaining children finish before a song advances once to its next position.
Power loss after consumption can leave an operation unexecuted or its result
unknown, rather than risk sending it twice.

## Required authoring and format changes

Setup format 19 and song records CSG4 replace the preceding formats. Recompile and
upload matching setup and song documents together before using this firmware.
Existing binary documents are rejected; no migration or dual execution path is
provided. Keep backups before replacing existing device data.
Exports after on-pedal program or bank edits preserve the current MIDI values
and displayed labels while canonicalizing their source metadata. Further
export/recompile cycles are stable.

- Move the old browser-list dictionary from `patches` to `patch_cycles`.
  `patches` now holds reusable named Patch tables.
- Rename `groups` to `patch_cycles`, `group.name` to `patch_cycle.name`, and
  `categories` membership and style definitions to `tags`. Program catalogues
  use the same `tags` membership field as patches.
- Add `type` to every executable table. Replace delimiter shorthand with explicit
  program fields; strings are not references and bare lists are not sequences.
- Replace `function = 'stock-firmware'` with `type = 'stock-firmware'`.
- Replace `actions` with `switches`, and each old command with its complete Patch
  table. References use stable keys rather than display names. Use `pages.switches`
  and `switch.*` display fields for switch presentation.
- Replace song instrument/program references with `type = 'reference'`,
  `document` and `patch`. Inline program selections use the song library's
  instrument context.

Automated tests and target builds qualify encoding, scheduling, source lifetimes
and continuation logic. Physical MIDI reception, switch timing and stock-return
behavior for this format still require separate hardware qualification.
