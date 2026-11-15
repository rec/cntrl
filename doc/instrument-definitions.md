# Creating and filling instrument definitions

Run `cntrl` or `cntrl setup create` to create a setup project. Press Enter to
accept each suggested value. The directory defaults to the current directory;
`cntrl setup create -o DIRECTORY` chooses it in advance. Give each instrument a
name (up to 31 filename-safe ASCII characters), its controller MIDI output name,
and its MIDI receive channel. Leave the
next instrument name blank when finished. Review the file list before accepting.
Existing destination files are never overwritten. Cancelling before confirmation
writes nothing.

The project contains a setup TOML file, an `instruments/` directory, and this
guide. Instrument files are setup fragments referenced directly by the setup:

```toml
instrument_files = ['instruments/synth.toml', 'instruments/keys.toml']
```

Paths are relative to the file declaring them. Referenced files are loaded in
list order, followed by that file's own settings. Tables merge by field, and
local values override referenced values. Lists combine using the existing
source-composition rules. References can also appear in system defaults and
instrument fragments. Missing files and reference cycles report errors.

Compile or copy just the setup; no instrument list is needed on the command line:

```sh
cntrl compile setup.toml --target TARGET.toml --output setup.bin
```

Replace `TARGET.toml` with the target description for your controller. Output
names such as `din` must occur in that target. To upload, use the same source
expression with `cntrl setup copy` and supply `--target` and `--port` instead of
`--output`. Merely placing a file in `instruments/` does not load it; list it in
`instrument_files`. Backups retain the referenced source files, and setup sync
can place reviewed edits back into their instrument fragments.

## System defaults

The wizard reads the same per-user `global.toml` as compilation and reports its
location when present. Inherited instruments get empty local fragments; do not add
them again. Fill those fragments only for setup-local additions or overrides.
The wizard does not modify that file or copy inherited preferences
into the setup. Built-in preferences also remain implicit.

Setup fields override global fields. Tables merge by field; lists replace.
An omitted list inherits, while an explicit empty list clears the inherited
list. An empty table does not remove inherited entries. Global catalogue and
asset paths remain relative to their declaring file. See
[global rig defaults](https://github.com/rec/cntrl/blob/main/doc/global-config.md).

## Instructions for an agent filling these files

Ask the user for the precise instrument manufacturer, model, firmware version
where relevant, MIDI connection, and any expansion banks or user sounds. Read
its official MIDI implementation and patch list. Cite the manual or source in
comments in the instrument file. Do not invent program names, controller numbers,
bank mappings, SysEx, or support for features absent from the manual. Leave
unknown information commented out and explain what remains unknown.

Preserve the user's existing setup and system defaults. Edit the instrument's
fragment to add its routing, program names, and any verified bank mapping. Keep
its alias consistent with setup patch references. Do not change `global.toml`
unless the user asks for rig-wide changes. Prefer comments explaining the
instrument's useful controls to large tables of guessed or unused settings.

Routing uses this schema (the instrument identity defaults to the alias):

```toml
[instruments.synth]
output = 'din'
channel = 1
# instrument = 'model-name' # Optional shared identity, not a display label.
# profile = 'default'      # Omit for the ordinary default profile.
```

Channels are **1-16**. Program numbers in cntrl are **1-128**, even when the
manual describes the MIDI wire values as 0-127. MIDI CC numbers, CC values and
bank MSB/LSB values are **0-127**.

For an instrument with no bank-select requirements, add a list directly to the
routing table:

```toml
programs = ['First sound', {}, 'Third sound']
```

Array position sets the program number, starting at 1. `{}` skips an unknown
position without shifting later program numbers. Do not populate fake entries.

If bank selection is required, use bank tables instead of that list:

```toml
[instruments.synth.programs]
0 = ['First sound', {}, 'Third sound']
1 = ['Another sound']

[[definitions.profiles]]
instrument = 'synth'
name = 'default'

[definitions.profiles.banks.0]
msb = 0
lsb = 0

[definitions.profiles.banks.1]
msb = 0
lsb = 1
```

These addresses are examples only. Each bank key is a logical cntrl bank number;
its profile mapping supplies the actual CC 0 MSB and/or CC 32 LSB. Specify only
the components that the instrument requires. The profile's `instrument` must
match the instrument identity, which normally matches the alias. Multiple
instrument fragments can each contribute `[[definitions.profiles]]` entries
through `instrument_files`. A local `definitions.profiles` list replaces the global
list, so include every inherited profile still needed, exactly once. Do not
define the same instrument/profile pair twice.

Add performance choices to the main setup separately, for example:

```toml
[patches.clean]
instrument = 'synth'
program = 1
# bank = 0 # Only if a bank was defined and mapped above.

[patch_cycles]
Live = ['clean']
```

For a verified control change, a setup patch can instead be:

```toml
[patches.volume]
type = 'control-change'
output = 'din'
channel = 1
controller = 7
value = 100
```

Use names meaningful to the user. No new performance patches are generated by
the wizard, so it sends no invented instrument messages. Compile using the
project's recorded command after editing. Compilation checks schema and target
routing; it cannot establish that the chosen numbers match the physical
instrument. Ask the user to verify the result on their instrument.
