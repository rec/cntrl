# Multiple named performance setups

Status: proposed implementation plan, 2026-10-24. No implementation or hardware
changes are part of this document. Resolve the decisions identified below before
implementing the affected behavior.

## Goal and terminology

Replace the single active performance configuration with a collection of named
setups on the pedal's SD card. Compile setups on the development machine, copy
them to the pedal, select them persistently, download them, and edit them from
the pedal itself.

A **performance setup** is what the existing application calls a show/config:
instrument routes, categories, patches and display/behavior settings. It is not
the existing instrument-level `setup` field, which identifies instrument bank
addressing rules. Do not rename or conflate that field as part of this feature.

TOML and JSON inputs are henceforth called **data files**. The development
machine reads setup data files, instrument definitions, patch tables and target
descriptions using the common composition/naming mechanism below.

## Data file expressions and names

Anywhere a data file is accepted, two or more data files can be combined with
` + `: a plus symbol surrounded by spaces. Load their dictionaries and combine
them into one input before compiling or validating the complete setup. Preserve
the specified operand order; do not sort the files before combining them. TOML
and JSON files can participate in the same expression.

Separate positional expressions still produce separate setups. For example:

```bash
ctrl setup copy jazz.toml Evening=theatre.toml
```

This copies two setups: `jazz`, from `jazz.toml`, and `Evening`, from
`theatre.toml`. A name is introduced by `NAME=`, not by a `--name` option.
Names are destination metadata, not new keys inserted into the data dictionaries.

Without an explicit name, use the source file's stem. For a combined expression,
join its source stems with `+`, in operand order:

```bash
ctrl setup copy jazz.toml + classical.toml
```

This combines the two dictionaries and copies **one** setup named
`jazz+classical`. The generated name contains no spaces around its `+`.
Three or more operands work the same way. A mixed-extension expression such as
`jazz.toml + classical.json` has the same default name.

An equals sign inside a name can be escaped in either of the two requested ways:
`==` or `\=`. For example, both commands name the setup `A=B`:

```bash
ctrl setup copy 'A==B=theatre.toml'
ctrl setup copy 'A\=B=theatre.toml'
```

Recognize escaped equals signs while locating the name separator; do not split
blindly at the first `=`. Shell quoting must preserve the intended argument and,
for `\=`, pass the backslash through to the application. Quoting alone does not
make an unescaped equals sign literal to the application's parser.

On the CLI, recognize the standalone `+` argument as the composition operator.
Do not split filenames at an embedded plus: `jazz+classical.toml` names one file.
Preserve quoted filenames containing spaces as individual operands. Reject
leading, trailing or repeated composition operators with useful diagnostics.
Use the same mechanism for definitions, tables and other data-file inputs, not
a separate parser or alternate naming syntax for each command.

Composition combines dictionaries, but its collision rules have not yet been
specified. Resolve shallow versus recursive combination, duplicate keys, list
values, incompatible types and conflicting scalar values before implementation.
Do not silently choose last-file-wins or list concatenation. Also resolve the
scope of an explicit name when it appears alongside `+`; the single-file named
example and the unnamed combined example above are agreed, not a definition of
every mixed naming/composition case.

Keep each constituent file's origin while loading. In particular, a patch-table
record's missing `instrument` still defaults to its own containing file's stem,
not the generated combined name or an explicit destination setup name. Resolve
that filename-based value before combining records. Do not apply ordinary model
defaults independently to partial data files before merging, since those
defaults could replace explicitly supplied values from another operand.

## Required command behavior

The requested spelling below is `ctrl setup`. The installed executable is
currently `cntrl`. Settle the spelling before changing the CLI entry point; do
not silently rename it or introduce a second alias.

### `ctrl setup copy`

- Accept one or more positional data-file expressions using the shared ` + `
  composition and `NAME=` naming syntax above.
- Reuse the existing definitions, target and repeatable table inputs to resolve
  instruments and compile each setup on the development machine.
- Parse every constituent data file, combine each expression using the agreed
  dictionary rules, then validate the resulting setups. Collect errors across
  files and records and report all errors before starting any upload. A partial
  constituent is not required to be a complete setup on its own.
- Reject duplicate destination names in the batch before writing anything.
- Copy the compiled results into the pedal's setup directory over the existing
  USB connection. This is a data transfer, not a firmware update.
- Report a durable, read-back-verified result for each setup. If storage or USB
  fails after earlier setups were committed, report the successes and failure
  accurately. Do not claim the whole batch is atomic.

Existing-name replacement, including replacing the active setup, needs an
explicit policy before implementation. Proposed default: report a collision
instead of silently overwriting. Also confirm whether copying the first setup
to an empty pedal should activate it; copying other setups should not implicitly
change the active selection.

### `ctrl setup set [NAME]`

- With no argument, print only the current `setup_name` stored in NV. This query
  must not restart the GUI or modify storage.
- With a name, look for that setup on the SD card. An absent name is an error.
- Validate and stage the setup before changing the persistent selection. An
  unusable setup must not displace the currently working one.
- Persist and verify `setup_name`, then restart the GUI with that setup. This
  means reinitializing the application interface, not rebooting the processor
  or entering the firmware bootloader.
- Apply all setup settings and clear the previous setup's browsing/history and
  known instrument selections, using the existing activation behavior. Do not
  send a patch merely because a setup was selected.
- On a persistence failure, leave the previous selection and GUI intact. Once
  the selection is durable, a reset before activation should boot the new setup.

### `ctrl setup list`

List all stored setups, one per line, with their last-modified dates. Use the
same alphabetical order as boot selection and rotation. Display dates in the
development machine's local time as `YYYY-MM-DD HH:MM:SS`, without milliseconds
or a timezone suffix. Obtain the catalogue and dates from the pedal, not local
source-file mtimes.

### `ctrl setup del NAME...`

- Accept one or more names and validate the entire request before deleting any.
- Show the names and ask `Are you sure?` once for the batch.
- `-y` / `--yes` skips that confirmation, not validation or safety checks.
- Without an interactive confirmation channel, require `--yes` rather than
  hanging or treating EOF as consent.
- Delete only the named application's setup records. Never delete unrelated
  SD files, manufacturer data, animations or the containing storage root.
- Report partial deletion failures accurately.

Deleting the active setup, deleting the final setup, and handling the selection
record during deletion are unresolved. Do not implement an implicit fallback or
delete a selected setup until this policy is agreed.

### `ctrl setup rotate`

Move to the next setup in alphabetical order, wrapping at the end. With
`--backward` / `-b`, move to the previous setup, also wrapping. Return an error
when fewer than two setups exist. Use the same validation, persistent selection
and GUI restart operation as `set`; do not maintain a separate rotation index.

### Downloading

Download is required by the overall request but has no specified subcommand.
Proposed interface: `ctrl setup download NAME... --directory PATH`.
Confirm the verb and downloaded representation before implementing it.
Downloads must reflect the latest committed setup, including on-pedal edits,
and be verified for completeness and checksum. Existing local files must not
be overwritten silently.

## SD catalogue and durable selection

Use one application-owned directory, proposed as `/cntrl/setups/`, with one
directory per named setup. Extend the existing storage boundary to address
named setups rather than hard-coding a single pair of root filenames.

Reuse the current two-generation write, commit marker, checksum and read-back
verification pattern within each setup directory. Readers select the newest
valid committed generation; temporary or incomplete writes are not setups.
Only one setup needs to be loaded into the existing active/staging buffers at a
time. Do not allocate a complete document buffer for every stored setup.

Persist setup name, format version and last-modified information with the
committed setup generation so the catalogue cannot expose mismatched metadata
after an interrupted save. Derive the catalogue from committed records rather
than introducing an independently mutable index file.

Treat names as logical identifiers, never arbitrary paths. Enforce uniqueness
and reject traversal, separators and unsupported names before any filesystem
operation. Decide maximum name length and alphabetical comparison rules before
implementation. In particular, filesystem case behavior must not let `Jazz`
and `jazz` overwrite one another. Proposed ordering is case-insensitive ASCII,
with case-only duplicate names rejected; confirm this rather than silently
choosing a collation.

`setup_name` must survive power loss in the chosen nonvolatile backend:

1. At GUI startup, load the stored name.
2. If no name has been stored, choose the alphabetically earliest available
   setup, persist that name, then activate it.
3. If a name is stored, load that setup rather than guessing from SD order.

The request does not specify an empty card, a stored name whose setup has gone
missing, or a corrupt selected setup. Define those outcomes explicitly. Do not
silently equate a missing/corrupt selection with an unset selection.

**NV backend decision:** the current application persists documents on SD and
has no selection-name NV implementation. Confirm whether NV means a separate
EEPROM/flash-backed record or any persistent storage. An application-owned SD
selection record is the least invasive option if acceptable. If separate NV is
required, establish a safe, owned location first. Do not guess unused EEPROM
addresses, assume flash emulation is harmless, or overwrite vendor settings.
Write selection changes only when necessary, with integrity checking and
recovery from an interrupted update.

Keep existing single-show files untouched. Any import/migration of those files
needs a specific policy; do not delete or silently relabel them.

## Editable representation and on-pedal operation

The existing compiled document is not a lossless representation of the original
source files. It resolves regexes and table joins, stores MIDI addressing bytes,
and omits some original instrument/setup metadata. Downloading that binary alone
cannot recover the source TOML/JSON or the original expressions.

Before defining the new final format, decide whether downloads must provide:

- A compiled setup for exact backup/reinstallation; or
- An editable, self-contained setup representing the current resolved state.

The recommended editable representation is the resolved state, not a promise to
reconstruct original regexes or private development-machine file paths. Retain
the identities and addressing metadata it needs in the new final format.
Have one authoritative committed setup state; avoid separately editable source
and binary copies that can disagree after an on-pedal edit.

On the pedal, setup management must use the same catalogue, selection and save
operations as USB commands. Define a reachable selection/editing UI without
silently repurposing existing musical buttons or reserved chords. Selecting a
setup or editing it must not emit accidental MIDI through the management
gesture. Finish already accepted output batches before switching routes, and
consume management-button releases before normal controls resume.

Define the editable fields and Save/Cancel behavior before implementing the
editor. A save validates the complete result, commits a new generation and
updates modification metadata. Cancel leaves the stored generation unchanged.
Switching away with unsaved edits needs an explicit discard/save policy.

Text/font edits need particular care: font size and name offsets are currently
computed on the development machine. On-pedal changes that affect layout must
recompute and validate those derived values with the available font metrics,
including the shared font size, before committing. Do not save stale offsets
or silently clip names. The edit-field decision determines how much target-side
layout work is actually required.

Copy operations can supply modification timestamps from the development
machine. On-pedal saves need a valid wall clock; uptime alone cannot produce
an honest calendar date after a power cycle. Decide clock synchronization and
the display of unknown dates before promising dates for offline edits. Do not
fabricate timestamps from build time or 1970 defaults.

## Implementation sequence

1. Resolve CLI spelling, dictionary combination rules, explicit-name scope in
   combined expressions, naming/order rules, replacement and deletion policies,
   NV ownership and boot failure behavior. Decide download representation,
   editing scope and timestamps before designing their formats.
2. Define the self-contained final format and integrity-protected selection
   record. Reuse existing Pydantic validation and compiler rules; keep TOML/JSON
   parsing, shared data-file expression handling and instrument-table resolution
   on the development machine.
3. Replace single-document storage/activation with named setup operations in
   the portable firmware. Keep hardware-specific SD/NV/USB bindings and tests
   under `cntrl/maker/morningstar/mc/`.
4. Extend the existing USB router and bounded transfer protocol for catalogue,
   copy, download, selection, rotation and deletion. Length-frame names and
   payloads so spaces and file bytes cannot become commands. Preserve timeout,
   checksum and read-back verification. Do not add arbitrary file access.
5. Add the nested setup commands using tyro and Pydantic command models. Keep
   name selection and rotation authoritative on the pedal, not in a stale host
   cache. Replace superseded single-setup operations rather than adding a
   competing state-management path.
6. Implement the agreed pedal selection/editing UI and the shared durable save
   path. Keep button scanning and MIDI servicing responsive during SD work.
7. Complete host tests and target builds/inspection. Perform upload, reboot and
   physical UI qualification as a separate explicitly requested hardware step.

## Verification and acceptance

- Separate inputs versus ` + ` groups; two, three or more operands; mixed TOML
  and JSON; combined default names preserving operand order.
- `NAME=FILE` naming; `==` and `\=` inside names; shell-preserved backslashes;
  quoted filenames with spaces; literal plus signs inside filenames; malformed
  composition operators and the agreed explicit-name scoping rules.
- Agreed dictionary/list/scalar collision rules, partial inputs and shared
  behavior at every data-file input. Filename-derived instrument defaults must
  retain their original file context rather than using a composite/setup name.
- Duplicate/case-colliding/unsafe destination names; errors accumulated across
  all constituent files and resulting setups before upload.
- Unset NV selects alphabetical first, persists and survives restart; existing
  NV is honored; failed selection writes preserve the old active setup.
- `set` query has no writes or GUI side effects; absent/invalid targets fail.
- One-line sorted listings and timestamp formatting without zone/milliseconds.
- Forward/backward wrapping; zero/one setup errors; consistent persisted name.
- Confirmed and declined deletes, `--yes`, noninteractive refusal, missing
  names and the agreed active/final-setup policies.
- Interrupted copy/edit/delete/selection commits, bad checksums, unsupported
  formats, card-full errors and partial multi-setup results without data loss.
- Downloads match committed state and reflect on-pedal edits; any editable
  export can be recompiled according to its agreed round-trip contract.
- Pedal Save/Cancel, management gesture suppression, no MIDI on activation,
  clean history/UI reset, and valid text layout after editable-field changes.

Do not mark the feature complete after CLI uploads alone: persistent selection,
downloads and the agreed on-pedal editing workflow are part of the goal.

## Additional work beyond the prompt

None.
