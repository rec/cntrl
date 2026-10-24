# Multiple named performance setups

Status: implementation plan with agreed decisions, 2026-10-24. No implementation
or hardware changes are part of this document. The final review section records
implementation risks; the interaction decisions are resolved.

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
cntrl setup copy jazz.toml Evening=theatre.toml
```

This copies two setups: `jazz`, from `jazz.toml`, and `Evening`, from
`theatre.toml`. A name is introduced by `NAME=`, not by a `--name` option.
Names are destination metadata, not new keys inserted into the data dictionaries.

Without an explicit name, use the source file's stem. For a combined expression,
join its source stems with `+`, in operand order:

```bash
cntrl setup copy jazz.toml + classical.toml
```

This combines the two dictionaries and copies **one** setup named
`jazz+classical`. The generated name contains no spaces around its `+`.
Three or more operands work the same way. A mixed-extension expression such as
`jazz.toml + classical.json` has the same default name.

An equals sign inside a name can be escaped in either of the two requested ways:
`==` or `\=`. For example, both commands name the setup `A=B`:

```bash
cntrl setup copy 'A==B=theatre.toml'
cntrl setup copy 'A\=B=theatre.toml'
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

### Combination policy (resolved)

Add `avoid_confict: bool = True`, preserving that spelling. Dictionaries merge
recursively in operand order in both modes. Keys present in only one dictionary
are retained.

| Overlapping values | `avoid_confict = True` (default) | `avoid_confict = False` |
| --- | --- | --- |
| Dictionaries | Merge recursively | Merge recursively |
| Lists | Concatenate in operand order, including repeated entries | Retain one copy if equal; otherwise report an error |
| Scalars | Use the later value | Retain the value if equal; otherwise report an error |

Incompatible value types remain errors. Do not stop at the first conflict:
inspect all keys, nested dictionaries and remaining operands, accumulate all
errors across all expressions, then report them together and terminate without
uploading anything. Diagnostics must identify the source data files and the
conflicting key paths. Lists in strict mode are compared as complete ordered
values, not concatenated or deduplicated.

Expose `avoid_confict` as one host-side CLI setting applying to every merge in
the invocation. Do not select the policy from the data files being merged.

An explicit name at the start names the entire expression:
`cntrl setup copy Evening=jazz.toml + classical.toml` creates `Evening`.
A name after `+` is an error, not a name for an individual operand.

Keep each constituent file's origin while loading. In particular, a patch-table
record's missing `instrument` still defaults to its own containing file's stem,
not the generated combined name or an explicit destination setup name. Resolve
that filename-based value before combining records. Do not apply ordinary model
defaults independently to partial data files before merging, since those
defaults could replace explicitly supplied values from another operand.

## Required command behavior

The executable is `cntrl`. References to `ctrl` in requests are spelling errors,
not a request to rename the executable or introduce an alias.

### `cntrl setup copy`

- Accept one or more positional data-file expressions using the shared ` + `
  composition and `NAME=` naming syntax above.
- Reuse the existing definitions, target and repeatable table inputs to resolve
  instruments and compile each setup on the development machine.
- Parse every constituent data file, combine each expression using the agreed
  dictionary rules, then validate the resulting setups. Collect errors across
  files and records and report all errors before starting any upload. A partial
  constituent is not required to be a complete setup on its own.
- Reject duplicate destination names in the batch before writing anything.
- Existing destination names are errors unless `--overwrite` / `-o` is set.
  That flag permits replacement, not duplicate names within the same batch.
- Copy the compiled results into the pedal's setup directory over the existing
  USB connection. This is a data transfer, not a firmware update.
- Report a durable, read-back-verified result for each setup. If storage or USB
  fails after earlier setups were committed, report the successes and failure
  accurately. Do not claim the whole batch is atomic.

Validate replacement data before touching the existing committed generation.
After a verified replacement of the active setup, reload its GUI immediately,
without sending MIDI. On an initially empty pedal, activate the alphabetically
earliest successfully copied setup after the batch finishes, including a batch
that finishes with a reported partial failure. Otherwise copying does not change
the selected setup, except to reload an explicitly overwritten active setup.

### `cntrl setup set [NAME]`

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

### `cntrl setup list`

List all stored setups, one per line, with their last-modified dates. Use the
same alphabetical order as boot selection and rotation. Display dates in the
development machine's local time as `YYYY-MM-DD HH:MM:SS`, without milliseconds
or a timezone suffix. Obtain the catalogue and dates from the pedal, not local
source-file mtimes.

### `cntrl setup del NAME...`

- Accept one or more names and validate the entire request before deleting any.
- Show the names and ask `Are you sure?` once for the batch.
- `-y` / `--yes` skips that confirmation, not validation or safety checks.
- Without an interactive confirmation channel, require `--yes` rather than
  hanging or treating EOF as consent.
- Delete only the named application's setup records. Never delete unrelated
  SD files, manufacturer data, animations or the containing storage root.
- Report partial deletion failures accurately.

Allow deletion of the active setup after confirmation. Select and persist the
alphabetically earliest valid setup outside the entire deletion batch before
removing the active setup. If none remains, clear the selection safely and enter
the no-setups state. A failed selection change must not remove the active setup.
Deleting the last setup is allowed. An empty catalogue is a supported state
throughout the system, not a special case that assumes a setup still exists.

### `cntrl setup rotate`

Move to the next setup in alphabetical order, wrapping at the end. With
`--backward` / `-b`, move to the previous setup, also wrapping. Return an error
when fewer than two setups exist. Use the same validation, persistent selection
and GUI restart operation as `set`; do not maintain a separate rotation index.

### Downloading

Use `cntrl setup download NAME... --directory PATH`, with `--directory` defaulting
to `.`. Export editable, self-contained resolved TOML, including on-pedal edits,
not the original source expressions or machine-specific paths.
Downloads must reflect the latest committed setup, including on-pedal edits,
and be verified for completeness and checksum. Write one `NAME.toml` per setup.
Existing local files are errors unless `--overwrite` / `-o` is set. Validate
names and destination collisions for the entire request before writing files;
do not leave a truncated existing file after a failed transfer.

## SD catalogue and durable selection

Use one application-owned directory, `/cntrl/setups/`, with one
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
operation. Names are 1 to 63 printable ASCII characters, including spaces, `+`
and `=`, but excluding SD-invalid filename characters (`<>:"/\\|?*`), `.` and
`..`, and trailing spaces or periods that cannot be represented reliably on the
SD filesystem. Preserve capitalization for display; sort and compare identities
case-insensitively using ASCII. Reject case-only duplicates such as `Jazz` and
`jazz`, including names generated from combined file stems.

`setup_name` must survive power loss in the chosen nonvolatile backend:

1. At GUI startup, load the stored name.
2. If no name has been stored, choose the alphabetically earliest available
   setup, persist that name, then activate it.
3. If a name is stored, load that setup rather than guessing from SD order.

When zero valid setups exist, show `No setups` in a large font, centered in
each of the three displays. Keep USB setup management and the pedal management
entry gesture available; normal performance controls send no MIDI. This must
work at boot, after final deletion, and after discovering that all stored
records are unusable. A missing/corrupt selected setup with other valid setups
available reports an error and preserves the stored name until explicit
selection; do not silently activate another performance setup.

Use an application-owned SD selection record as NV, not a separate EEPROM or
flash allocation. Do not guess unused EEPROM addresses or overwrite vendor settings.
Write selection changes only when necessary, with integrity checking and
recovery from an interrupted update.

Keep existing single-show files untouched. Do not migrate or activate them
automatically. Users copy their original source data files through the new CLI.

## Editable representation and on-pedal operation

The existing compiled document is not a lossless representation of the original
source files. It resolves regexes and table joins, stores MIDI addressing bytes,
and omits some original instrument/setup metadata. Downloading that binary alone
cannot recover the source TOML/JSON or the original expressions.

Downloads provide an editable, self-contained setup representing resolved state,
not a promise to reconstruct original regexes or private development-machine
file paths. Retain
the identities and addressing metadata it needs in the new final format.
Have one authoritative committed setup state; avoid separately editable source
and binary copies that can disagree after an on-pedal edit.

On the pedal, setup management must use the same catalogue, selection and save
operations as USB commands. Open management with the A+D chord; use A/B to
browse, C to confirm, and F to go back. Selecting a setup or editing it must
not emit accidental MIDI through the management
gesture. Finish already accepted output batches before switching routes, and
consume management-button releases before normal controls resume.

The first editor supports numeric program, bank, channel, volume, pan,
brightness and timeout values. Exclude text editing, renaming, reordering,
routing, colors and fonts from this release. Use explicit Save/Cancel. A save
validates the complete result, commits a new generation and updates modification
metadata. Cancel leaves the stored generation unchanged.
Switching away with unsaved edits offers Save/Discard/Cancel. Do not introduce
target-side text layout changes for this numeric-only editor.

Modification dates mean successful setup commit time, not source-file mtimes.
Synchronize wall time from the Mac when connected. Without a verified persistent
clock, offline edits after a reboot have an unknown date until time is synced;
print `unknown` rather than fabricating a calendar timestamp from uptime, build
time or 1970. Do not invent dates retroactively for edits made without a clock.

## Implementation sequence

1. Preserve the second-press management-chord rule recorded below.
   Specify resolved export metadata and edit ownership using existing models
   before designing the final format.
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
- Recursive dictionary merging in both `avoid_confict` modes; default list
  concatenation and later-scalar precedence; strict equal lists/scalars retained
  once; strict differing values and incompatible types reported together across
  nested paths, later operands and separate expressions. No upload on errors.
- Partial inputs and shared behavior at every data-file input.
  Filename-derived instrument defaults must
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
  clean history/UI reset, and numeric edits preserving text layout.
- Copy/download collisions with and without `--overwrite` / `-o`; failed
  replacement preserves the previous committed setup or local file.
- Zero valid setups at boot and after deletion/corruption; centered `No setups`
  on all three screens; silent performance controls and usable USB/management.
- Active deletion in a batch never selects another setup scheduled for deletion.
- Unknown dates before time sync, and commit-time dates after synchronization.

Do not mark the feature complete after CLI uploads alone: persistent selection,
downloads and the agreed on-pedal editing workflow are part of the goal.

## Final review: essential implementation risks

- **A+D versus existing gestures:** opening management must not first execute
  either button's ordinary action. Reserve the chord in the existing chord
  recognition path and test press ordering, holds and releases. During the
  screensaver-entry fade, the existing rule consumes the whole wake gesture.
  A+D used to wake the pedal does not open management. Consume that complete
  wake gesture, including releases, and require a second A+D after waking.
- **Deletion interruption:** clearing selection before deleting the final setup
  is insufficient by itself: a reset could reselect the still-present record.
  Define a recoverable deletion commit/order so a completed deletion stays
  deleted and interrupted operations never leave a selection pointing to a
  removed record. Cover power loss at each storage boundary.
- **Round-trip and edit ownership:** a resolved TOML export must recompile
  without the original definitions/tables and reproduce the same effective
  routes, patches and settings. Specify whether each numeric editor field
  belongs to a patch, an instrument or the setup, following existing model
  ownership. In particular, an absent bank must remain representable, and
  volume/pan edits must not silently inject live MIDI.
- **Concurrent USB and pedal edits:** do not overwrite an unsaved pedal edit
  through a host copy, delete or selection command. Use the same dirty-state
  gate for both entry points; a host operation that cannot present the pedal's
  Save/Discard/Cancel choice should report the busy/unsaved-edit condition.
- **No-setup display independence:** the empty/error-state UI needs a built-in
  readable font and colors because there is no active setup from which to load
  them. Do not dereference setup settings when booting an empty catalogue.
  Recognize A+D without requiring a valid active document; the current
  performance controller ignores button events when its document is invalid.
- **Usable versus merely present records:** distinguish an empty usable
  catalogue from card access failure, invalid records and an unsupported format.
  Report those errors over USB even when the displays must say `No setups`;
  never delete or silently overwrite damaged records as automatic recovery.

## Additional work beyond the prompt

None.
