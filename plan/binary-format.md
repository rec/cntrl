# Released persistent formats, starting with f0

## Decision and compatibility promise

Call the first released persistent format **f0**. Existing development version
numbers are not released format names. Freeze f0 only after the inventory,
specification, converters and recovery tests below are complete.

Use extensible records for compatible additions. Introduce f1, f2 and subsequent
formats only when an encoding or meaning must change incompatibly. Retain a
host-side converter for every adjacent released pair, fN to fN+1. A current host
must be able to read and upgrade every released format by following that chain.

These are two separate guarantees:

- New software reads old released data with its original meaning, including
  the defaults that applied when it was written.
- Old software accepts newer data only when it understands every feature needed
  to use that data correctly. Otherwise it refuses activation and preserves it.

An additive feature can remain in f0 while requiring a capability that the first
f0 firmware lacks. Supporting the f0 envelope alone does not imply support for
every future f0 feature. Backward compatibility does not imply that old firmware
can run new features.

The host performs released-format conversion. Firmware supports its advertised
format and capabilities without carrying an accumulating chain of converters.
Deployment must select a format supported by the actual firmware. There is no
automatic downgrade that drops unsupported behavior.

## Persistent-file inventory

This inventory describes the current storage contracts. Paths identify existing
files, not a requirement to preserve today's directory layout forever. During
implementation, record each producer, consumer, envelope and recovery rule in
the format specification. Inspect adapter storage as well as generic storage.

| Persistent data | Current location or representation | f0 treatment |
| --- | --- | --- |
| Authoring configuration | Composed TOML inputs, included definitions, hardware descriptions and referenced source assets on the host | Preserve original files, composition and ownership. Binary conversion must not flatten them into one replacement source file. Version authoring syntax separately when necessary. |
| Compiled setup bundle | `/cntrl/setups/.bundle/0.bin` and `1.bin`, journaled `CSTB` bundle containing `CSW1` documents | Convert every setup and its settings, patches, bindings, MIDI processing, apps, pages and asset references. Regenerate sorted tables, offsets, setup indices and RAM estimates. |
| Per-setup save records | `/cntrl/setups/<name>/0.bin` and `1.bin`, `CNST` storage records | Inventory their relationship to the active bundle and import any authoritative saved content. Preserve the selected committed generation rather than treating every slot as a separate user setup. |
| Setup history | `/cntrl/history/<name>/<16-hex-revision>/0.bin` and `1.bin`, `CREV` records with setup payloads and name tables | Preserve revision identity, label, timestamp, setup references and content. Empty revision directories reserve numbers after deletion and must survive migration. Preserve original archive bytes alongside converted representations. |
| Selected setup | `/cntrl/selection/0.bin` and `1.bin` | Preserve the selected setup's identity. Resolve through the converted name table instead of copying a stale numeric index. |
| Songs and setlists | `/cntrl/songs/0.bin` through `3.bin`, committed `CSG5` generations and a streaming import slot, with `CSP5` payload data | Convert order, repetitions, selections and captured setup documents. Preserve the valid undo generation where present. Slot 3 is uncommitted input, not another active library. |
| Saved execution continuation | Adapter-owned nonvolatile records for returning to a song or setup | Inventory through the adapter API. Remap durable references, verify the referenced generation, and avoid replaying an operation whose completion is uncertain. Do not infer that every live cursor is currently durable. |
| Font assets | `/cntrl/fonts/<key>.cfont`, currently `CFN2`, plus host font assets and original outline fonts | Preserve referenced asset bytes even when the original font is unavailable. Distinguish retained source material from any raster cache that can actually be rebuilt. |
| Animation assets | `/cntrl/movies/<key>.bin`, host `.cnsa` assets, currently `CNSA` v2, and any original movies | Preserve referenced assets. Target-specific frame encoding may change independently if the retained asset can still be decoded or converted. Upload staging files are uncommitted. |
| MIDI recordings and supplied files | `/cntrl/midi/*.mid`, including `<Starts>-<recording_count>.mid` | Keep Standard MIDI Files in their standard format. Preserve filenames, musical events and timing; copy unchanged bytes when no conversion is needed. Preserve how the most recent completed recording is selected. |
| Restart counter | `/cntrl/starts.toml` | Preserve its monotonic value and recording-name uniqueness. Recovery or rollback must not overwrite a recording by reusing a name. |
| Host backup sets | Setup binaries and exports, `songs.bin`, history binaries, `history.toml`, `storage.toml`, selection records and companion assets | Read old backups permanently. Retain originals and write conversion results separately. Preserve deleted-history reservations and report recorded missing assets rather than declaring an incomplete backup complete. |
| Host synchronization state | Project `setup-sync/`, including `base.toml`, baseline binaries, snapshots, source copies, `snapshot.toml`, `complete.toml`, `review.toml`, `proposal.toml`, `applied.toml` and `pending.toml` | Preserve source provenance, baselines, decisions and recovery before-images. Rebuild format-dependent hashes consistently. Resolve an interrupted apply before converting; regenerate an unapplied review if conversion changes its comparison inputs. |
| Adapter-owned device state | Boot selection, calibration, firmware return state and any other nonvolatile adapter records | Each adapter declares export, restore and compatibility rules. Keep opaque state opaque unless that adapter supplies a converter. Never copy calibration blindly between physical devices. |
| Deployment and recovery artifacts | Firmware images, build/target manifests and known-good recovery packages | Retain the firmware/data pairing needed for rollback. Ordinary build outputs are reproducible only when their sources, toolchain identity and assets are available. |

The following are derived or transient, subject to verifying that they contain
no sole surviving user content:

- Bundle offsets, target memory layouts, RAM estimates and loaded setup caches.
- MIDI playback indices, including `.playback-index` and stream sidecars.
- Interrupted upload staging files and uncommitted journal generations.
- Live MIDI state, pending events, gesture state and other runtime state that
  is not currently saved. The current live looper generations are RAM buffers.

Do not create persistence for transient state as part of f0. A partial recording
may contain recoverable user input, so preserve it separately when discoverable;
never mistake it for a completed recording or a disposable playback index.

Inventory evidence lives in `cntrl/patch_switcher/compiler.py`,
`setup_bundle.py`, `songs.py`, `backups.py`, `setup_sync.py`,
`cntrl/animation/asset.py` and the generic firmware headers `setup_files.h`,
`bundled_storage.h`, `revisions.h`, `songs.h`, `startup_counter.h`,
`asset_storage.h`, `midi_files.h` and `midi_loop_file.h`. Adapter paths and
nonvolatile layouts remain documented beside their implementations.

## Stable data and fast runtime access

Specify durable bytes independently of native C++ structs, compiler packing,
pointer width, CPU endianness and hardware memory placement. Use explicit
little-endian integers and documented byte offsets. A C++ structure layout is
never the specification.

Keep one authoritative logical document for each setup or library. It includes
everything necessary to preserve pedal edits and reconstruct executable data.
The host authoring composition remains separate, with synchronization provenance
linking edits back to its original files.

Lower durable documents into the existing fast indexed runtime representation.
Compiled tables may carry a separate cache ABI and target fingerprint. They are
usable only when both match; otherwise regenerate them before activation. A
cache ABI change alone does not require f1. The implementation must demonstrate
that reconstruction fits the device's memory and storage budgets.

On-pedal saves and history must retain the authoritative document, including
opaque optional records. Updating a runtime cache alone is not a durable save.
Avoid a second independently editable representation or a new edit-log model.

Durable cross-document references use names or explicit stable identities whose
scope and rename behavior are specified. Bundle indices and node offsets remain
derived. Every indexed legacy reference must be resolved using its own original
table before conversion, including references inside old revisions and captured
song setups.

## f0 envelope and record rules

Write a normative byte specification and checked-in fixtures before freezing
f0. It must fix integer widths, field offsets, integrity algorithms, tag numbers,
limits and example encodings. The rules below are requirements for that spec,
not permission to change the format after release.

1. Use one documented envelope family with an artifact kind, released format
   number, header length, payload length and integrity check. f0 is numeric zero.
   Identify setup, bundle, library and other cntrl-owned artifact kinds explicitly.
   A standard `.mid` file retains its standard envelope.
2. Integrity covers the meaningful header and complete payload, including
   unknown records. Specify exactly which checksum bytes are excluded or zeroed.
   A checksum detects corruption; it is not an authenticity guarantee.
3. Encode extensible content as typed, length-delimited records. Each record has
   a permanent numeric tag, flags and payload length. Nested records obey the
   same rules. Define header extension handling and reject unknown flag bits
   unless the spec explicitly makes them ignorable.
4. Maintain a checked-in registry of artifact kinds, record tags, capabilities,
   units, defaults and cardinality. Never reuse retired identifiers. Define
   singleton duplicates as invalid; repeated records preserve their specified
   order. Define which collections are ordered and which are keyed.
5. Freeze the meaning of an absent field. A later change in a Python default
   cannot change an old file's behavior. New writers emit an explicit value when
   their preferred default differs from the frozen absence rule. Specify units,
   signedness, ranges, float encoding and rounding wherever they affect behavior.
6. Define UTF-8 encoding, string lengths, identity comparison and invalid-text
   handling. Preserve legal user names; do not silently normalize distinct names
   into one identity. No implicit native padding or NUL-termination assumptions.
7. Specify format limits separately from device resource limits. Validate counts,
   lengths, offsets, nesting, references and arithmetic before allocation or
   activation. A device that lacks memory reports the requirement; it does not
   truncate content. Integrity failure never becomes an empty valid setup.
8. Writers produce deterministic bytes for the same understood content. Opaque
   records retain their bytes and relative placement wherever placement matters.
   Fix the checksum and ordering rules so host and firmware agree exactly.

Do not place a new version number on every field. The released format is the
breaking-change boundary; permanent tags and capabilities handle additions.
Asset codecs retain explicit codec identifiers and their existing versions.
Container compatibility does not imply that a device supports every codec.

## Extensibility and safe editing

Every added record must declare whether understanding it is necessary for
correct execution or editing. The document advertises required capabilities,
and records also mark required semantics so a nested unknown operation cannot
be accidentally skipped.

| Change | Compatibility rule |
| --- | --- |
| New descriptive metadata | Optional when ignoring it cannot change execution. Preserve it on save. |
| New setting with unchanged absence behavior | Same format; require a capability when its presence changes behavior an older implementation cannot reproduce. |
| New operation, gesture, processor or asset codec | Same format when representable by the existing envelope; advertise the required capability. Refuse activation if unsupported. |
| Changed units, absence semantics, tag meaning or reference interpretation | Use a new tag with explicit semantics if that fully preserves old behavior; otherwise introduce the next released format and converter. |
| Different native memory layout or rebuilt index | Change the runtime cache ABI without changing the durable format. |
| Incompatible envelope or record encoding | Introduce the next released format and converter. |

Unknown optional metadata can be skipped for execution and must survive a
read/edit/save round trip, including within understood parent records. Optional
does not mean safe to delete. A firmware editor unable to retain it must refuse
that save and explain why. Raw backup/download remains available.

An unknown executable action must fail activation of the affected setup or
library before any MIDI is sent. Do not run a partial patch, silently bypass a
processor or substitute a default operation. Other supported setups may remain
available.

Unknown records cannot contain undocumented references to mutable indices.
Extension specifications must declare their reference representation. If an
editor or converter cannot prove that a rename, deletion or remapping preserves
an opaque record's references, it refuses the affected transformation. Required
capabilities cover the transitive dependency closure, including captured setups
and referenced assets.

## Converters between released formats

Keep a small host conversion registry for artifact kinds and adjacent released
formats. Each converter reads the frozen source schema and produces the frozen
next schema. Its behavior must not depend on today's application model defaults.
Shared implementation is fine when frozen behavior remains testable.

For an f0 backup opened by f3 software, run f0 to f1, f1 to f2, then f2 to f3.
Keep all three converters in releases that support f3. Converted documents record
their source format and conversion provenance in the enclosing conversion report;
do not burden every musical operation with migration metadata.

Converters must:

- Preserve musical behavior, ordering, timing units, labels, edits, identities
  and unknown optional records when their semantics remain valid.
- Convert nested setup documents, history, song captures and synchronization
  baselines, not just the currently selected setup.
- Rebuild derived tables and integrity values; update understood references and
  asset keys together if their encoding changes. Reuse unchanged asset bytes.
- Produce deterministic results with a report identifying each input, output,
  format, digest and any issue requiring a decision.
- Fail with an artifact path and reason when conversion is unsupported,
  ambiguous, corrupt or lossy. Never approximate an operation, silently reset a
  value or delete unknown data to make conversion succeed.

Preserve original backups and archived revision bytes. A converted revision can
be restored using its converted representation while the original remains
exportable. Keep revision numbers and deletion reservations stable. A repaired
converter needs a regression fixture; previously converted copies are not
silently rewritten on startup.

Before f0, provide an explicit importer for the development formats actually
needed by retained backups and deployed devices. Inventory those inputs and
publish the supported list. Do not promise a converter for every historical
development build or relabel an old binary as f0. Importers are separate from
the permanent released-format chain.

## Conversion, deployment and recovery transaction

Use the existing backup, synchronization and deployment utilities. Extend their
contracts to cover the inventory rather than introducing another independent
backup mechanism. Host editing and pedal editing need not run concurrently.
Power loss, interrupted transfer and mismatched firmware still require recovery.

1. Discover firmware format/capability support and enumerate the full persistent
   dependency closure. Include history, song captures, assets, reserved revision
   numbers, relevant adapter state and host synchronization state. Resolve any
   pending synchronization apply before proceeding.
2. Make and verify an immutable original backup, plus the known-good firmware
   identity and recovery procedure. Report missing assets and corrupt records.
   Preserve damaged bytes for recovery; do not silently omit them or claim a
   complete migration while required content is missing.
3. Convert into a separate staged generation. Validate every artifact, nested
   reference and capability against the intended firmware. Check device RAM and
   storage capacity, including temporary copies and the retained recovery set.
4. Transfer and read back the complete staged generation. Publish a manifest
   identifying its artifacts, digests and firmware compatibility. Reuse existing
   slot journals for individual records, but do not assume independent slot
   commits make a multi-file upgrade atomic.
5. Activate only a complete compatible generation through one recoverable commit
   point. Boot must select an intact firmware/data pairing or enter a documented
   recovery path. Firmware flashing and filesystem updates are separate physical
   operations; the deployment sequence must handle interruption between them.
6. Verify startup, setup selection, history restore, song captures, asset access
   and a representative pedal edit/save/reload. Retain the old generation until
   the new one is accepted. Update host baselines only after successful activation.

Define generation selection, storage flush guarantees and recovery explicitly
for each adapter. Do not rely on a filesystem rename being power-loss atomic
without that guarantee. Failure before commit leaves the old generation usable;
failure after commit selects the verified new generation or its recovery pair.

Rollback restores the old compatible firmware/data pair. If new user edits have
occurred, back them up first and explain that older firmware may not execute
them. Preserve restart counters and recording-name uniqueness across rollback.
No automatic reverse converter is promised.

## Qualification and release gates

- Check in a format specification, tag/capability registry and small golden
  fixtures for every artifact kind. Keep fixtures from every released format,
  with expected decoded meaning and converter output.
- Cross-check Python writers and C++ readers against identical byte fixtures on
  supported hosts. Test absent fields, unknown optional fields at every nesting
  level, duplicate fields, required capabilities and unsupported codecs.
- Exercise read/edit/save with unknown data retained and transformations refused
  when opaque references cannot be preserved.
- Test truncated records, corrupted headers and payloads, oversized counts,
  overflowing offsets, invalid references and malformed text before allocation
  or MIDI execution. Verify explicit errors and unchanged source files.
- Test the complete converter chain, including histories using older setup
  tables, deleted revision reservations, captured song documents, missing source
  assets and synchronization baselines. Compare resulting musical behavior,
  including MIDI ordering, timing, bindings and inherited patches.
- Interrupt conversion, transfer, slot writes, manifest publication and activation
  at each durable boundary. Verify a usable old or new generation and preserve
  recoverable original data.
- Measure file size, load time, edit/save cost and peak RAM on the existing
  device. Streaming and opaque-record preservation must fit real budgets.
  Host tests alone do not establish hardware or power-loss behavior.

The release gate is a demonstrated round trip from real pre-release backups to
f0, through pedal edits, backup, history restore and source synchronization, plus
successful recovery from interrupted deployment. Tag the first public release
with its exact f0 specification and fixtures. All later released changes are
checked against that frozen contract.

## Implementation sequence

1. Complete the producer/consumer inventory and collect real development-format
   samples. Document which bytes are authoritative and which are regenerable.
2. Specify the f0 envelope, records, defaults, reference rules and capability
   registry. Freeze byte fixtures after reviewing size and device constraints.
3. Implement host and firmware codecs and runtime lowering. Replace existing
   persistence paths consistently, including pedal saves and nested documents.
4. Implement the required development importers and the adjacent-format converter
   interface. Integrate backups, history, assets and synchronization state.
5. Integrate staged deployment, compatible-generation activation and adapter
   recovery using the existing utilities. Complete interruption qualification.
6. Run the release gates and publish f0. Add f0 to f1 only when an actual breaking
   requirement appears; keep compatible additions within f0.

Each step should remain reviewable and may require several focused commits.
This document plans the work; writing it does not freeze the current binary
layout or perform a device migration.

## Additional work beyond the prompt

None.
