# Global defaults for complete setups

Status: implemented. Automated qualification passes; physical pedal qualification
has not been performed.

The shared authoring pipeline now resolves global defaults and recovered session
recipes before setup validation. Upload retains exact source snapshots, while
sync writes inherited edits locally. Source-aware download uses the visible
`cntrl-session.toml` manifest and restores original sources, baseline images and
saved pedal images without the original machine or its per-user configuration.

Validation passed 1783 tests with one skip, including native source-transfer,
sync, song, history and backup/restore coverage. Ruff, formatting, type checking
and pyupgrade checks pass. Normal and diagnostic firmware links retain the
existing 4096-byte ITCM growth reserve. No application or hardware was launched
for physical qualification.

## Implementation format decision

Source archives use a version-1 host manifest inside an immutable archive stored
at `/cntrl/source/<sha256>.bin`. The source association is a known optional
setup-envelope record, tag 2, containing the 32-byte archive SHA-256. It adds
40 bytes including its record header, not the source payload, to a setup image.
It is singleton metadata with no graph indexes; unknown records retain the
existing opaque-metadata restrictions. No performance-core or deployment-journal
format change is needed.

Upload and read-back verify the immutable archive before publishing its reference
through the existing atomic bundle replacement. Setup save/history retain the
reference with the image. This ties source publication to the existing setup
transaction without staging unrelated history and song stores again. Archive
baselines omit their own source-reference record to avoid a circular hash.

Allow a user to define their normal rig once and author each setup as a small
set of changes. Defaults must cover every authorable setup field, including
instruments, patches, MIDI routes, definitions, counters, parameters, switches,
pages, regions, styles, assets and ordinary settings. Do not maintain a whitelist
of fields eligible for inheritance.

Hardware target declarations, device-local expression calibration and compiled
firmware constants remain separate. This feature supplies defaults for setup
documents; it does not turn hardware capabilities into setup preferences.

## Agreed decisions

- Apply defaults during compilation. Each compiled setup remains complete and
  self-contained; the pedal does not merge configuration during setup loading.
- Allow defaults for all setup content, including the user's instruments and
  other rig definitions. Merge fields within named definitions recursively.
- Use explicit removal lists to remove inherited values or definitions.
- Automatically load a per-user `global.toml` for ordinary authoring.
- Sync pedal edits to inherited values as setup-local overrides, preserving the
  shared defaults file.
- Transfer the global source and the rest of the required authoring session to
  the pedal, so recovery does not depend on the original computer.
- Retain each uploaded setup's exact source snapshot. Uploading another setup
  with newer defaults must not destroy the sources needed by older setups.
- Download a self-contained session directory. Its recorded defaults take
  precedence over the recovering computer's per-user defaults, without installing
  or modifying that computer's defaults.

## Precedence and presence

Resolve configuration in this order, from weakest to strongest:

1. Built-in model defaults, used only for fields still absent after resolution.
2. Explicit fields from the selected global configuration.
3. Explicit fields from the setup's existing data-file expression.

Operationally, load the global and setup documents as raw authoring data,
compose the setup's own files using the existing rules, overlay the resulting
setup on the global data, then validate and fill model defaults once. Do not
instantiate `Setup` separately for either layer and merge their default-filled
exports. A partial global definition can be completed by the setup.

An explicit value wins even when it equals the built-in default. In particular,
`false`, zero, empty strings and empty lists are values, not absence. Preserve
each field's existing validation and meaning; the merge does not make invalid
values valid.

`pickup = "none"` or `"None"` is an explicit override retaining pickup's existing
inheritance semantics, not a generic deletion marker. Other fields do not gain
string or null-based inheritance syntax.

## Merge rules

- When both values are tables, merge recursively by key. This includes named
  instruments, patches, routes, pages, counters and other definitions.
- When the setup supplies a scalar, list, or a value of a different structural
  type, replace the inherited value with the setup value and validate the result.
- Lists replace as a whole, including ordered patch sequences, route lists,
  program lists, page cycles and nested lists. Do not append or merge by index.
- An empty list clears an inherited list. An empty table supplies no field
  overrides; clearing or removing inherited tables uses the explicit removal
  mechanism below.
- Do not infer field removal when a discriminated definition changes type.
  Remaining inherited fields can make the result invalid. Report that validation
  error and allow explicit removal followed by a complete replacement.
- Resolve each setup from an independent copy of the global document. Compiling
  one setup must not mutate defaults or leak overrides into another setup.

These rules apply between the global and setup layers. Existing `+` composition
within a setup expression remains unchanged, including its conflict checks and
list behavior. The precedence between layers is intentional and must not be
controlled by the existing `avoid_confict` option.

### Example: one rig, two small changes

The global file uses ordinary setup syntax:

```toml
brightness = 160
pickup = true

[instruments.synth]
output = 'din'
channel = 1

[patches.sound]
instrument = 'synth'
program = 10

[page_settings]
clock_outputs = ['din']
```

A setup changes only the channel, program and clock output list:

```toml
[instruments.synth]
channel = 2

[patches.sound]
program = 12

[page_settings]
clock_outputs = []
```

The resolved setup retains brightness 160, pickup, the instrument's output and
the patch's instrument reference, while using channel 2 and program 12. Its
clock output list is empty. Defaults do not force duplicated rig declarations.
Examples must be checked against the current authoring schema during
implementation.

## Explicit removal

Reserve host-only authoring metadata at `inheritance.remove`:

```toml
[inheritance]
remove = [
    ['patches', 'unused'],
    ['instruments', 'spare'],
]
```

Each entry is a nonempty list of literal table keys. Keys containing dots remain
unambiguous; do not introduce dotted-path parsing or list-index editing. Apply
removals to the inherited data before applying setup overrides. A setup can
therefore remove an inherited definition and supply a complete replacement at
the same path. Removing a table removes its subtree. Removing a scalar returns
that field to its built-in default unless the setup supplies a replacement.

Reject missing paths, attempts to traverse non-table values, and redundant
duplicate/ancestor removals with source locations. A stale removal should not
silently hide a rig change. Validate references after removal and overlay, so
deleting a referenced instrument or patch produces the normal reference error.
Strip this metadata before normal setup validation and compilation. It is not
a firmware setting. Do not also assign removal semantics to empty tables.

## Loading and command integration

Automatically load one per-user TOML file at the standard configuration location:

| Platform | Location |
| --- | --- |
| macOS | `~/Library/Application Support/cntrl/global.toml` |
| Linux | `$XDG_CONFIG_HOME/cntrl/global.toml`, or `~/.config/cntrl/global.toml` when XDG_CONFIG_HOME is unset or empty |
| Windows | `%APPDATA%/cntrl/global.toml`, or `~/AppData/Roaming/cntrl/global.toml` when APPDATA is unset or empty |

A missing per-user file means no authored global defaults. A present unreadable
or malformed file is an error. Do not silently ignore it, create the file on a
read, or add another command-line/environment override mechanism.

For recovered sessions, use the defaults snapshot associated with each root
setup in the session manifest instead. It replaces the per-user layer, rather
than adding another inheritance layer. A manifest can explicitly record that
the setup was compiled without a global file; that also excludes the recovering
computer's global defaults. Do not search arbitrary parent directories for
other `global.toml` files. Session membership and defaults bindings are explicit
in the recovered manifest, although selection is automatic when loading its
root setup documents. A missing recorded snapshot is a recovery error, not a
reason to fall back to per-user defaults.

Reuse the existing document loader for setup sources. Normalize asset and
catalogue reference paths relative to the file that authored them before merging,
including inherited references. Defer expansion that requires a complete
definition until after overlay. Preserve original source bytes and origins for
review and write-back. Both global and setup authoring can be partial; validate
their effective combination, not each partial definition in isolation.

The shared resolver must serve compile, setup copy and setup sync, and the
emulator's source-compilation path. Apply the same defaults independently to
referenced companion setups before discovering their dependencies, validating
references and calculating bundle residency. Follow the existing setup-reference
cycle and deduplication rules; defaults must not create a separate resolver.

Song commands that compile embedded setup contexts must use the same resolution
rules for those contexts and companion setups. A song/setlist library's own
schema and its list of songs are not setup fields and are not overlaid with the
global setup document. Low-level binary verification and restore of already
resolved setups must not reapply defaults.

Separate target, definitions and table inputs retain their existing purposes.
Inline setup definitions inherited from globals follow the same relationship
to those inputs as explicitly authored inline definitions do today. Do not add
a second precedence system for them.

## Compiled output and source provenance

With host-time resolution, the pedal receives a fully resolved setup using the
existing binary/runtime settings. Loading it needs no global file and performs
no new runtime merge. Changing the global file affects subsequent compilation
only. The user must recompile and copy every setup that should receive the
change; previously uploaded setups remain self-contained.

Preserve source presence and origins separately from effective values. Record
the selected defaults source, presence, snapshot/checksum and resolution policy
in the existing compile recipe and synchronization records. Treat the file and
its referenced sources as shared dependencies of all consuming setups, including
companions. Recipes must record what was actually read, rather than rediscovering
the current per-user file during review/apply.

An ordinary new compilation reads the current per-user defaults. Applying a
review rechecks the captured sources and reports changes rather than silently
using new defaults. A recovered session reads its bound snapshot instead.
Use the existing metadata facilities rather than inventing another history store.

## Pedal storage and recoverable sessions

Transfer a source archive as part of each verified setup upload. It must contain:

- Exact authored global-file bytes, or an explicit record that no file was used.
- Original setup overrides and every source file participating in their existing
  `+` composition, including removal metadata and companion setups.
- Target, definitions and table sources, their referenced catalogues, and the
  compile recipe needed to reconstruct the session.
- Required font, movie and MIDI assets, reusing existing asset transfer/storage
  instead of storing another copy of assets already retained on the pedal.
- A TOML manifest binding each setup and its compiled baseline to its source
  snapshot, file checksums, source origins and compiler/schema identity.

Capture the actual source bytes used by compilation. A file changed between
compile and upload must not become the alleged source of the compiled image.
The archive must be sufficient to rebuild on another machine with the matching
software; original absolute paths and files on the original machine are not
dependencies. Preserve original bytes with a portable source/path mapping rather
than silently rewriting the archived originals. Recovery must reconstruct the
effective configuration and assets; byte-identical binaries also require the
same compiler/build identity.

Store source payloads on SD, outside the performance setup's resident memory.
Transfer and verify them through the existing file/deployment machinery. Do not
embed whole source archives in every runtime `Document` or load them on setup
selection. Firmware management support for archive transfer, storage, bindings
and retrieval is in scope; a runtime defaults resolver is not.

Snapshots are immutable once published. Identical files/snapshots can share
stored content by checksum, but each setup retains its exact binding. Replacing
or uploading one setup must preserve unrelated setups' bindings and sources.
Keep source snapshots referenced by retained setup/history images. Do not add
an automatic source-pruning policy in this implementation.

Publish the archive and its setup association through the existing verified
deployment commit/recovery workflow. Interrupted uploads must not make a new
setup appear recoverable against the wrong defaults. Check required source and
asset availability before publication; insufficient SD space or failed verification
fails the upload without replacing the previously verified association. Include
source archives and associations in backup/restore and verification.

Download/recovery writes a self-contained session directory with a TOML manifest,
root setup documents, recorded `global.toml` snapshots, source dependencies and
assets. Multiple defaults versions stay separate and are associated with their
respective setups. A session-aware compile resolves each setup using its recorded
snapshot automatically; changing the recovering machine's global file has no
effect. Do not install recovered sources into the per-user configuration folder.

Retain existing destination checks and explicit overwrite handling for download.
Missing or corrupt sources must be reported precisely. Already compiled setups
remain usable, and existing resolved export remains available, but do not claim
exact source recovery for an older upload that never retained its authoring files.
Do not try to reconstruct omissions by comparing resolved values with defaults.

Saved pedal edits are changes to the compiled baseline, not changes to archived
authoring sources. Recovery retains that baseline and the saved pedal state so
the existing reviewed sync process can place the edits into the recovered setup
as local overrides. It must not discard saved edits or claim that the original
source snapshot alone represents the latest saved state.

## Synchronization

A pedal edit to an inherited value is proposed in that setup's authored source,
not written into the global file.
Deleting an inherited named item requires a local removal entry; clearing a
list writes an explicit empty list. An unchanged inherited value must remain
omitted rather than being materialized during sync. Existing explicitly shared
setup sources retain their current reviewed shared-edit behavior.

The three-way review must include global source changes and recheck all source
checksums before apply. Preserve its existing conflict and recovery workflow.
Source-aware recovery preserves inheritance using the recorded provenance;
ordinary resolved exports remain standalone and do not invent it.

## Implementation sequence

1. Add per-user discovery, recorded session bindings and raw-data overlay/removal
   logic in the existing authoring pipeline. Keep and reuse setup composition.
2. Wire all setup compilation entry points, companion discovery, origin-relative
   assets and source tracking through the same resolution stage.
3. Integrate recipes, dependency tracking and reviewed sync write-back. Retain
   authored omissions and never silently edit the shared rig.
4. Extend verified upload/deployment, backup/restore and download to retain and
   recover source snapshots and bindings without adding resident playback RAM.
5. Test recovery on another directory/machine configuration, including setups
   uploaded with different global snapshots and saved pedal edits.
6. Document rig/default authoring, automatic discovery, list replacement,
   deletion, recompilation and source-aware session recovery. Add compact examples.

No dependency changes are expected. Keep performance setup encoding and execution
unchanged where possible; extend the existing management/deployment metadata for
source bindings. If a persistent management format needs a version change,
document that change and its recovery/verification requirements before coding it.

## Verification and acceptance

Use focused behavior tests, consolidating cases with existing data-file and
sync tests where possible. Isolate per-user configuration directories in tests
so a developer's real rig cannot affect their results:

- With no global file or session manifest, existing inputs retain their behavior
  and compiled output. Per-user discovery follows the documented platform paths.
- A small setup inherits a complete rig and overrides nested instrument, patch,
  route, page and ordinary setting fields without repeating the remaining fields.
- Omitted values inherit; explicit built-in-valued scalars, false, zero, empty
  strings, empty lists and pickup's explicit None semantics retain their meanings.
- Lists replace rather than append across the layer boundary. Existing `+`
  composition within the setup expression retains its behavior.
- Deletion, subtree replacement, literal dotted keys, missing removal paths,
  changed definition types and broken references produce the agreed results.
- Two setups share defaults without mutating one another. Invalid effective
  configurations fail before device transport starts, with useful source paths.
- Partial definitions are validated only after overlay. Global-relative fonts,
  movies and catalogues resolve at their original source.
  Companion setup discovery sees inherited references and applies defaults once.
- Compile, copy, sync, emulator source compilation and embedded song setup
  compilation agree on effective values. Already resolved restore is unaffected.
- Sync preserves omissions, places inherited pedal changes as agreed, detects
  changed shared defaults and retains existing apply/recovery guarantees.
- Exact source recovery includes the defaults, overrides, target, catalogues,
  recipe and assets used by that upload, independent of subsequent host changes.
- Mixed uploads retain their respective defaults versions. Missing/corrupt
  snapshots, failed transfers, interrupted publication and backup/restore preserve
  the existing verified setup/source associations or report a precise failure.
- A recovered session recompiles with a different or absent per-user global
  file and no access to the original source paths. Its bindings also preserve
  an explicit absence of defaults. Recovering never overwrites per-user defaults.
- Saved pedal edits are retained and reviewed as local overrides during recovery.
  Resolved export and already compiled restore remain independent of source files.
- Source archive size does not become part of the resident performance setup's
  memory estimate or its setup-load allocations.

Run the required Python checks for implementation changes. This planning-only
commit needs a document/diff review, not firmware builds, application launches
or hardware access.

## Additional work beyond the prompt

None.
