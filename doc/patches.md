# Patches, switches and songs

`patch_cycles` maps names to ordered lists of patches. A tag classifies a program
or patch independently of those lists. Items use a `tags` list; the setup's
optional `[tag_styles.NAME]` tables define styles for selected tags. Tags do not
need a declaration to be used. Top-level `tags` describes the performance setup
itself and is not inherited by its patches.

A Patch can be a string naming a local patch or a table. Tables use `patch`
for references with additional fields, `patches` for a sequence, `operation`
for controller actions, and `type` for MIDI messages.
Each form works in a named definition, browser patch cycle, switch assignment,
song position or sequence.
A sequence executes all its children in order on one activation. A song
advances one position at a time.

## Patch window presentation

The patch-manager window is divided into three equal vertical bands. The top
band is reversed across its full width. Its background uses the heading's
resolved patch color and its text uses the page background. The second and third displayed
lines use the smaller of their fitted sizes, retaining their font families.
If the families have different bitmap sizes, the largest shared size at or
below that limit is used. Switch pages retain their independent line sizing.

Styles may set `horizontal_scale` to a positive width multiplier. It participates
in the usual style cascade and overrides the selected font's default. StillTime
defaults to 1.5; other fonts default to 1.0. Fitting includes the scaled width.

## Automatic cycle styles

Setup-level `fill_in_styles = true` (the default) fills missing formatting for
each cycle and its program lines. Explicit show, window, page, tag, program,
patch and field styles still override these generated defaults per property.
Global parameter and switch fields keep their existing formatting.

```toml
seed = 0
fill_in_styles = true
background_color = '0xFFF8D8'
```

Every cycle uses the same pale yellow background by default. Each cycle
also gets a random available font, shared with its programs. All available font
families are used before repeating, with adjacent repeats avoided when possible.
Available families include target fonts and configured `[fonts]` entries.

For N cycles, foreground generation starts with the configured background
color as an anchor. It greedily chooses N other colors,
maximizing each new color's minimum squared RGB distance from the anchor and
earlier choices. Candidates are a bounded RGB grid with channel values
0, 32, 64, 96, 128, 160, 192, 224 and 255, restricted to weighted RGB brightness
below 50%: `(0.299 R + 0.587 G + 0.114 B) / 255 < 0.5`. This is an approximate
perceived-brightness measure, not linear-light luminance. Explicitly authored
foreground colors are unchanged. This is a farthest-point heuristic,
not a guarantee of the global optimum. The N foregrounds are distinct; the
background anchor is not assigned to a cycle. Existing contrast warnings remain.

One local random generator controls all these choices. Integer `seed` defaults
to 0, making repeated compilation deterministic. Python callers may pass
`seed=None` for a fresh one-time seed; TOML has no null literal. The compiled
setup records that chosen seed, so downloading and recompiling preserves the
appearance. Set `fill_in_styles = false` to retain only authored styling and
application defaults. Styles are generated during compilation, with no pedal
random generator or new firmware record format.

## Simple program lists

For an instrument that stays in one bank, list its program names in order:

```toml
[instruments.synth]
channel = 1
output = 'din'
programs = ['Piano', 'Flute', {}, 'Strings']

[patch_cycles]
Live = [{name = 'Piano'}, {name = 'Flute'}, {name = 'Strings'}]
```

Array positions are program numbers 1 through 128. An empty table `{}` leaves
that position unnamed without moving subsequent programs. A flat array sends
Program Change only: it does not select bank 0 or send either bank-select
controller. Select the desired bank on the instrument before using the setup.

An entry can also be a metadata table, for example
`{name = 'Flute', tags = 'woodwind,solo'}`. Tags accept a list of strings or a
string separated by commas or colons. Program numbers and instrument identities
come from the containing structure and cannot be repeated inside these entries.

In a patch cycle, `{name = 'Flute'}` looks up an exact catalogue program name.
If a name occurs more than once, qualify it with
`{name = 'Flute', instrument = 'synth', bank = 3}`,
using whichever qualifiers are needed to select one program. These shorthand
entries can include presentation properties, volume and pan. Named patches,
switch assignments, sequences and songs accept patch operations and named references.

For explicit banks, use a table whose keys are bank numbers:

```toml
[instruments.synth]
channel = 1
output = 'din'

[instruments.synth.programs]
0 = ['Piano', 'Flute']
3 = ['Lead', 'Strings']

[[definitions.profiles]]
instrument = 'synth'
name = 'default'

[definitions.profiles.banks.0]
lsb = 0

[definitions.profiles.banks.3]
lsb = 3

[patch_cycles]
Live = [{name = 'Piano'}, {name = 'Lead'}]
```

Explicit banks need mappings in the instrument profile. `msb` is the value sent on
CC 0; `lsb` is the value sent on CC 32. Only the configured controllers are sent.
The example configures CC 32 alone. The compiler does not guess a device's bank
mapping from its name or a catalogue's bank numbers.

The instrument identity defaults to its table key, and `profile` defaults to
`'default'`. A profile with no bank mappings or initialization data needs no
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
If a nondefault profile is needed, declare `profile` once in the instrument table.
Changing a classification does not require editing the documented catalogue.

## Several catalogues on one MIDI destination

When two instruments receive the same Program Change, describe the route once
and attach their aligned sound catalogues:

```toml
[instruments.both]
output = 'din'
channel = 1
catalogues = ['vl70-internal.toml', 'patchman-internal.toml']

[patch_views.patch]
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
output = 'din'
channel = 1

[[definitions.profiles]]
instrument = 'piano'
name = 'default'

[patches.clean]
type = 'program-change'
instrument = 'piano'
program = 1
display_name = 'Clean piano'

[patches.intro]
patches = [
    {operation = 'brightness', value = 192},
    'clean',
    {type = 'control-change', output = 'trs', channel = 2, controller = 11, value = 100},
]

[patch_cycles]
Live = ['intro']

[switches.intro]
button = 'A'
on = 'intro'
```

Instrument declarations and their bank mappings retain their existing meaning.
Switch-only setups can omit instruments, definitions, named patches and patch cycles
when their patches do not use them. Named definitions have stable keys; their
dictionary order does not define the browser order. `patch_cycles` supplies that order.

A string is a named patch reference wherever a Patch is accepted, including
named definitions, browser cycles, sequence children, switch On/Off actions,
gesture actions, enter/exit hooks and song positions. For example,
`on = 'lead'` selects the local patch named `lead`. A missing name is an error,
even if a catalogue program has that name. Catalogue lookup uses an explicit
`{name = 'lead'}` table in a browser cycle.

Use a reference table when adding fields, such as
`{patch = 'lead', display_name = 'Solo'}` or
`{document = 'other-setup', patch = 'lead'}`. Editable exports normalize
references to tables, and those tables remain accepted by the shared parser.
See [string-references.toml](../examples/patch-switcher/string-references.toml)
for a complete setup.

## Derived patches in songs

A derived patch names a local patch as its `parent`. It inherits the parent's
fields and replaces the fields it specifies. Each override must be valid for
the parent's concrete patch type. A parent may itself derive from another
named patch. Forward references work; missing parents and cycles are errors.

```toml
[instruments.piano]
output = 'din'
channel = 1

[patches.A]
type = 'program-change'
instrument = 'piano'
program = 7
volume = 80

[patches."A′"]
parent = 'A'
volume = 100
display_name = "A'"

[[songs]]
name = 'Example'
patches = ['A′', 'A', 'A′']
```

Writing `type = 'derived'` alongside `parent` is optional. In the song list,
strings select named patches. Every press plays a complete selection, so A′
can be first. The example sends program 7 at volumes 100, 80 and 100.
The ASCII display name works with the classic font; the patch key uses the
typographic prime.

Inheritance is resolved when compiling the setup or song library. Editing A
and recompiling updates its variations. Downloading preserves the parent and
authored overrides. Volume and pan adjustments on the pedal export as overrides.
An edit that changes the program or bank, or clears an inherited setting,
exports the edited program in full.

## Implied MIDI message types

A patch may omit `type` when its fields identify the MIDI message.
For a Program Change without output/channel fields, `instrument` may also be
omitted when the setup contains exactly one instrument: `{program = 7}` selects
program 7 through that instrument's configured profile and output. With zero or
multiple instruments, specify `instrument` or use explicit `output` and `channel`
fields.

Other MIDI messages also inherit omitted routing fields from the sole instrument.
For example, `{controller = 7, value = 100}` uses its output and channel. SysEx
and system messages inherit only the output because they have no MIDI channel.
Explicit output or channel fields take precedence independently. Without exactly
one instrument, all routing fields required by the message must be explicit.

| Fields | Implied type |
| --- | --- |
| `program` | `program-change` |
| `controller`, `value` | `control-change` |
| `note`, `velocity` | `note-on` |
| `value` with no other message selector | `pitch-bend` |
| `parameter`, `value` | `nrpn` |
| `note`, `pressure` | `polyphonic-key-pressure` |
| `pressure` | `channel-pressure` |
| `piece`, `value` | `time-code-quarter-frame` |
| `position` | `song-position-pointer` |
| `song` | `song-select` |
| `data` | `sysex` |

For example, `{output = 'din', channel = 1, controller = 7, value = 100}`
sends CC 7 with value 100. `{instrument = 'piano', program = 7}` selects
program 7 through that instrument's configured profile and output.

Explicit types take precedence and keep their normal field validation. Use
`type = 'note-off'` to request Note Off. System events such as Start, Stop and
Timing Clock require their explicit types. Conflicting message selectors are
rejected rather than resolved by field order. Presentation fields such as
`name`, `display_name`, `tags` and `style` work with implied types.

Inference applies in named patches, patch cycles, sequences, switch assignments
and song positions. Tables with `parent`, `patch`, `patches`, `operation` or
`duration` keep their existing meanings. Downloads use the named MIDI types.

## Patch types

Routing fields in this table may be inferred from the sole instrument as described
above.

| Type | Required fields | Optional executable fields |
| --- | --- | --- |
| `program-change` (instrument) | `program`, `instrument` unless there is only one | `bank`, `volume`, `pan` |
| `control-change` | `output`, `channel`, `controller`, `value` | `resolution` (7 or 14) |
| `pitch-bend` | `output`, `channel`, `value` | None |
| `nrpn` | `output`, `channel`, `parameter`, `value` | `resolution` (7 or 14) |
| `rpn` | `output`, `channel`, `parameter` (1), `value` | None |
| `sysex` | `output`, `data` | None |
| `program-change` (direct) | `output`, `channel`, `program` (1..128) | None |
| `note-on`, `note-off` | `output`, `channel`, `note`, `velocity` | None |
| `polyphonic-key-pressure` | `output`, `channel`, `note`, `pressure` | None |
| `channel-pressure` | `output`, `channel`, `pressure` | None |
| `time-code-quarter-frame` | `output`, `piece` (0..7), `value` (0..15) | None |
| `song-position-pointer` | `output`, `position` (0..16383, in six-clock MIDI beats) | None |
| `song-select` | `output`, `song` (0..127) | None |
| `tune-request`, `timing-clock`, `start`, `continue`, `stop`, `active-sensing`, `system-reset` | `output` | None |
| Controller action (omit `type`) | `operation` and the fields below | None |
| Sequence (omit `type`) | `patches`, a list of patches, including string references | None |
| Named reference (omit `type`) | A string containing the stable key, or a table with `patch` | `document`, presentation fields in a table |

Every type can have `name`, `display_name`, `style` and `tags` presentation
metadata. Named definitions default their label to their key. Inline patches use
a description of the operation. References inherit the target's presentation;
metadata on the reference occurrence overrides it. Children of a sequence do not
replace the parent performance display.

Channels are 1..16. Programs use 1..128 or the existing program-name regular
expression lookup. Volume, CC controllers and CC values use 0..127; pan uses
-64..63. Pitch bend uses -8192..8191, with zero centered. NRPN parameters and values
use 0..16383. Program selections send bank MSB, bank LSB, program, optional volume
and optional pan, in that order. NRPN sends CC 99, 98, 6 and 38 as one ordered
message sequence on its output, allowing MIDI real-time traffic to interleave.
Fine CC uses `resolution = 14`, an MSB controller in 1..31 except 6, and a
value in 0..16383; it sends that controller followed by its LSB at controller+32.
NRPN defaults to resolution 14; `resolution = 7` accepts values 0..127.
The 7-bit form and `rpn` channel fine tuning send complete parameter selection
and data entry followed by RPN Null. Fine tuning uses RPN 1 and values 0..16383,
with 8192 neutral. These typed parameters are also available to
[LFO generators](lfos.md).

SysEx `data` contains 0..127 payload bytes, without F0/F7; the compiler adds those
delimiters. Notes, velocities and pressures use 0..127. Direct Program Change
sends a single message on its specified output and channel. The instrument form
resolves catalogue programs and supports bank, volume and pan.
Use the named message in authored setups and songs, for example
`{type = 'start', output = 'din'}` for MIDI Start. The former `type = 'midi'`
is rejected. Exporting an existing compiled message produces its named form.
Long SysEx streams through the normal cooperative output scheduler. The target
must support the named output; this format adds no output drivers.

## Controller operations

| Operation | Fields | Effect |
| --- | --- | --- |
| `stock-firmware` | `exit_buttons`, such as `'A+F'` | Switch to stock firmware with the specified return buttons |
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

Press assignments also accept a `gestures` table for release, long press,
double and triple taps, held second or third taps, and repetition, with matching
release actions.
See [switch controls](momentary-latched.md#programmable-gestures) for timing,
exclusivity and cancellation rules.

The full reachable graph identifies controls, including both branches and
referenced sequences. Instrument and direct program selections, Control Change,
Pitch Bend and NRPN use shared parameter state. Their indicators and latched
actions follow the recorded values, so aliases stay synchronized automatically.
Opaque MIDI operations such as SysEx reserve the entire output through Off cleanup or
explicit abandonment. Other assignments cannot share that output. There is no
automatic note ownership or inferred inverse. Accepted work completes before the
required Off patch runs. A held entry switch must be released before it rearms.

Stock visits and context-leaving operations are allowed in press assignments and
song positions. Either branch of a momentary or latched assignment must stay in
its context. Other active assignments finish their explicit cleanup before a
press assignment leaves. See [switch controls](momentary-latched.md).

Assignments can define `enter = { action = <Patch>, override = false }` and
`exit = <Patch>`. Selecting a different assignment runs exit, enter and the
selecting action in order. `enter.override = true` suppresses that selecting
action. Hooks follow selection independently of shared parameter state.
Hooks must remain in the switch context. See the [hook lifecycle](momentary-latched.md#enter-and-exit-hooks)
for repetition, cancellation and backpressure behavior.

## References and song libraries

Switch actions also accept `{ operation = 'shift' }` and
`{ operation = 'unshift' }` for the shared temporary assignment layer.
See [shifted actions](momentary-latched.md#shared-shift-layer) for alternate
actions, release pairing, cleanup and navigation behavior.

```toml
[patches.borrowed]
document = 'other-setup'
patch = 'clean'
```

External references use the setup's logical name and a named patch's stable key.
Display labels and instrument/program matching are not identities. The setup CLI
loads adjacent `other-setup.toml` companions recursively and compiles the complete
dependency graph before opening the transport.

The top-level setup setting `inline_patches = true` is the default. Referenced
patch actions and their nested dependencies are embedded in the compiled setup,
including source instrument routing and bank mappings. Executing them does not
load another setup from the pedal's card. Changes to a source setup require
recompiling and uploading its callers. Downloading an inlined setup exports the
embedded actions.

Set `inline_patches = false` to retain runtime references. These compile to
indices in the bundle's sorted setup table. Resident setups are reused without
disk reads; an LRU miss loads and validates the setup. Active invocations pin
their source views until completion. Missing references and cycles fail before
effects; pending work prevents replacement of its source data. This
setting controls patch dependencies within a compiled setup; song library live
references still resolve their source setup at playback time.

## Setup bundles and RAM2

`compile` and `setup copy` collect every requested setup and all transitive
companions into one bundle, sorted by case-insensitive logical name. Authoring
and UI retain the original names; executable cross-setup references use uint16
indices. `setup copy` preserves unrelated pedal setups, relinks the combined
bundle, and rejects unreconciled pedal edits in replaced setups. Use
`--overwrite` when replacing existing names. Verified copies establish the
project-local baseline for [source synchronization](setup-sync.md). Backup
restore also merges saved setups with the current collection and relinks indices.

The build reports each setup's compiled RAM size and `total_setup_ram`.
Each app and page declares `ram2_bytes`, defaulting to zero. The planner reserves
the largest combined app/page total for any one active setup, plus name/table/cache
metadata and a **288 KiB estimate** for cntrl's fixed and temporary RAM2 use.
That estimate includes document editing buffers, font and other runtime storage;
it is not a measured high-water value.

`total_free_ram2() = 524288 - memory_margin - total_ram2_needs()`.
The build option `--memory-margin` defaults to **16192 bytes** and accepts byte
quantities. Setup payloads are excluded from `total_ram2_needs()`.

If `total_setup_ram` fits, the bundle's count, offsets and setup bytes are loaded
into one resident `static_setups` slab with validated document views. Otherwise,
the same indices address a bounded LRU cache. Its initial contents use an exact
subset-sum calculation to maximize occupied bytes, rather than a largest-first
heuristic. A setup larger than the cache is a build error. If all possible
eviction candidates are pinned, a new acquisition fails without invalidating
the running patches.

On-pedal saves and per-setup history remain available. Saves build an inactive
bundle copy; the previous copy remains bootable until validation and completion.
History stores the name ordering so restoring an older setup translates its
indices into the current table. Deletes retain an empty index slot until the
next host rebuild. Captured song sources with runtime references carry a table
signature: rebuild/copy them again if the table's names change. A stale capture
fails before sending MIDI rather than addressing a different setup by accident.
Live song selections continue resolving their authored setup names at activation.
Saved pedal and host edits are reconciled explicitly with `cntrl setup sync`;
review and apply are separate from upload. Use the matching compiler and firmware
when deploying compiled setups.

Song libraries have optional `instruments`, `definitions`, `patches`, `fonts` and
`tags` context using the same authoring models. Each song's `patches` list
contains Patch strings or tables. Songs have unique names; setlists contain song
names, and playback tracks entry and patch indices. A nonempty library is compiled
with `--target`; companion setups and font assets follow the existing copy/download workflow. Inline captures
retain their compiled source; live references retain their stable document/key.

## Completion, failure and stock return

Execution is ordered across outputs. Backpressure leaves the current child
pending. A failed child stops the invocation; completed effects are not rolled
back. Completed locally means delivery to the local MIDI output, without an
instrument acknowledgement. Disconnects or abandoned cleanup can leave remote
state unknown. Undo restores known program selections and does not implicitly
replay a composite or reverse state, SysEx or firmware visits.

A stock visit checkpoints the enclosing root, completed child ordinal, source
fingerprint, caller, song and setlist names, and entry and patch indices when
present. It also preserves the browser context and active brightness/interface settings needed to resume.
Only an explicit stock return can resume. Its durable token is consumed before
dispatch; changed sources, invalid records and ordinary boot never replay it.
Remaining children finish before a song advances once to its next position.
Power loss after consumption can leave an operation unexecuted or its result
unknown, rather than risk sending it twice.

## Required authoring and format changes

The current setup format is 25; song records use CSG5. Setup
window graphics and page references use physical left-to-right order:
next (0), sent (1), undo (2). Recompile and
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
- Use `operation` for controller actions, `patch` for references, `patches` for
  sequences and `type` for MIDI messages.
  Replace delimiter shorthand with explicit program fields.
- Replace `function = 'stock-firmware'` or `type = 'stock-firmware'` with `operation = 'stock-firmware'`.
- Replace `actions` with `switches`, and each old command with its complete Patch
  table. References use stable keys rather than display names. Use `pages.switches`
  and `switch.*` display fields for switch presentation.
- Replace song instrument/program references with `document` and `patch`. Inline program
  selections use the song library's instrument context.

Automated tests and target builds qualify encoding, scheduling, source lifetimes
and continuation logic. Physical MIDI reception, switch timing and stock-return
behavior for this format still require separate hardware qualification.

## Delays between patch operations

A delay is a patch with a duration. Bare durations are seconds. Unit strings
such as `'100 ms'` and `'0.1 seconds'` are accepted, from 1 ms through 65.535
seconds in whole milliseconds. An explicit `type = 'delay'` is optional.

```toml
[patches.load-and-set-volume]
patches = [
    {type = 'program-change', output = 'din', channel = 1, program = 7},
    {duration = '100 ms'},
    {type = 'control-change', output = 'din', channel = 1, controller = 7, value = 100},
]
```

The wait starts when the executor reaches the delay, after the preceding
operation completes local output delivery. Instrument acknowledgement is not
part of MIDI delivery. A congested output therefore postpones the start of the
wait. The following operation waits for the full duration, including across
the millisecond clock rollover. Polling drives the wait without sleeping, so
other invocations, input handling, display updates and MIDI scheduling continue.

The invocation reserves the controls in its complete patch graph throughout
the wait. A delay itself reserves no additional MIDI controls. Canceling an
invocation discards its pending delay and following operations. Releasing a
momentary switch, toggling a latched switch off, or leaving Switches during an
On delay cancels that On sequence and runs its explicit Off patch. Off cleanup
may contain its own delays. An unavailable output follows the existing switch
unknown-state and explicit cleanup policy. Setup sources stay pinned while
an invocation is pending, and setup changes use the existing cancellation path.

Completed delays are counted in stock-firmware continuation checkpoints.
Returning from a stock visit skips waits already completed. Downloaded setups
and song libraries preserve delay durations in seconds using the short form.

## MIDI file playback and recording

The `play-midi-file` and `record-midi-file` operations are patches. See
[MIDI file patches](midi-files.md) for fields, timing, recording names and examples.

## MIDI PC/CC scroll counters

`scroll-counter` actions share named PC or CC values across assignments, with
configurable steps, wrap or bounce boundaries, and live display fields. See
[MIDI counters](counters.md) for configuration and delivery behavior.
