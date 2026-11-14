# Released persistent formats, starting with f0

## Compatibility contract

The first public format is **f0**. Existing development version numbers do not
become released formats. Freeze f0 only after its specification, importers and
recovery tests pass.

Use extensible records for compatible additions. Introduce f1, f2 and subsequent
formats only for incompatible encoding or semantic changes. Retain host-side
converters for every adjacent released pair, fN to fN+1. A current host must be
able to upgrade every released format without changing its musical meaning.

New software preserves the meaning and defaults of old files. Old software can
use newer files within the same format when it understands all required records.
Otherwise it refuses activation, preserves the bytes and allows raw backup.
An unknown format is never guessed from a similar layout.

Firmware need only support its advertised released format and features. The host
performs conversion before deployment and checks the actual firmware's support.
There is no promised downgrade converter or silent removal of unsupported features.

## Persistent-file inventory

These are existing storage contracts, not a requirement to retain every current
path. Before implementation, confirm their producers, consumers and authoritative
committed records. Adapter-specific layouts stay documented with their adapters.

| Data | Current representation | Preservation rule |
| --- | --- | --- |
| Authoring inputs | Composed TOML files, hardware descriptions and source assets | Keep original files, composition and source ownership. A binary upgrade must not flatten or replace them. Authoring syntax has its own compatibility rules. |
| Setup bundle and saved setups | `/cntrl/setups/bundle/{0,1}.bin`, `CNST` journals containing a `CSTB` bundle of `CSW1` documents; per-setup `/cntrl/setups/<name>/{0,1}.bin` records | Preserve every authoritative setup, including pedal edits, settings, patches, bindings, MIDI processing, apps, pages and references. Resolve precedence before importing old per-setup records. Recompute offsets and RAM estimates. |
| History and deleted revision reservations | `/cntrl/history/<name>/<16-hex-revision>/{0,1}.bin`, `CREV` records with setup documents and name tables; empty revision directories | Preserve revision numbers, labels, timestamps, content and reserved numbers. Keep original bytes in the backup and converted revisions available for restore. |
| Selected setup | `/cntrl/selection/{0,1}.bin` | Preserve setup identity through any table reordering. |
| Songs and setlists | `/cntrl/songs/{0,1,2,3}.bin`, `CSG5` committed generations with `CSP5` payloads; slot 3 is streaming import staging | Preserve order, repetitions, selections, captured setup documents and valid undo content. Do not activate an uncommitted import. |
| Fonts and animations | `/cntrl/fonts/<key>.cfont` (`CFN2`); `/cntrl/movies/<key>.bin` and host `.cnsa` files (`CNSA` v2); original assets where available | Keep referenced assets even when their original source is missing. Update keys and references together if conversion changes them. Retain existing codec identifiers and versions. |
| MIDI files and restart count | `/cntrl/midi/*.mid`, including `<Starts>-<recording_count>.mid`; `/cntrl/starts.toml` | Copy Standard MIDI Files unchanged. Preserve filenames, latest-completed-recording selection and recording-name uniqueness. Never roll the counter backward over recordings that still exist. |
| Host backups | Setup and history binaries/exports, `songs.bin`, `history.toml`, `storage.toml`, selection records and companion assets | Keep released backups readable indefinitely. Preserve deleted revision reservations and report missing assets. Convert into a separate destination. |
| Host synchronization state | Project `setup-sync/` baselines and binaries, source snapshots, `base.toml`, `snapshot.toml`, `complete.toml`, `review.toml`, `proposal.toml`, `applied.toml`, `pending.toml` | Preserve provenance, baselines and recovery before-images. Recover any pending apply first. Update converted baselines and hashes consistently; retain review choices for inspection and regenerate a review if its comparison inputs change. |
| Adapter nonvolatile state | Saved execution continuation, boot selection, calibration and firmware return state where implemented | Each adapter declares what must be exported or converted. Preserve opaque state unless its adapter supplies a converter. Do not replay an operation with uncertain completion or restore device calibration onto different hardware. |
| Recovery artifacts | Known-good firmware and build/target identity | Keep enough to restore a matching firmware/data pair using the existing deployment utilities. |

Derived data includes setup offsets, RAM estimates, loaded caches and MIDI
playback indices such as `.playback-index` and stream sidecars. Rebuild it only
when the authoritative inputs are available. A font or movie without its source
is still user content, not a disposable cache.

Live MIDI state, pending events, gestures and current RAM looper generations
remain transient. Uncommitted uploads are not active documents. Preserve any
recoverable partial recording separately and do not mark it completed. This
work does not add persistence to currently transient state.

Inventory evidence is in `cntrl/patch_switcher/` (`compiler.py`, `setup_bundle.py`,
`songs.py`, `backups.py`, `setup_sync.py`), `cntrl/animation/asset.py` and
`firmware/include/cntrl/` (`setup_files.h`, `bundled_storage.h`, `revisions.h`,
`songs.h`, `startup_counter.h`, `asset_storage.h`, `midi_files.h`,
`midi_loop_file.h`).

## One document format

Specify durable bytes independently of C++ structure packing, pointers and CPU
layout. Use explicit little-endian integers and documented byte offsets. Keep
the existing indexed access model where useful. An index is valid only with its
own document or saved reference table; converters resolve it using that table.

Use one authoritative serialized representation for a setup or library, including
pedal edits. Do not introduce a second portable document, a mandatory compiled
cache format or an edit log. Runtime views and rebuilt indices may differ from
the stored representation without changing its defined bytes or semantics.
Measure their load cost and memory use before freezing f0.

Host source composition and synchronization provenance remain separate existing
concerns. Binary conversion preserves them without inventing another source model.

## Encoding and extensibility rules

Before release, write a byte specification and fixtures fixing the following:

1. A small binary envelope with magic, artifact kind, released format number
   (zero for f0), header and payload lengths, and an integrity check. Define its
   fixed prefix so readers can identify and reject unsupported formats safely.
   Existing standard MIDI files, asset codecs and TOML files keep their own
   encoding; they need no new wrapper just to belong to an f0 deployment.
2. Length-delimited records with permanent numeric tags and one required flag.
   Define nesting, allowed flags, singleton/repeated cardinality and ordering.
   Reject duplicate singleton records and unspecified flag bits. Never reuse a tag.
3. A single registry specifying artifact kinds, tags, meaning, units, ranges,
   encoding and frozen defaults. Missing fields always retain their original
   meaning. New preferred defaults are written explicitly. Fix UTF-8 text rules,
   name comparison, float representation and rounding where applicable.
4. Integrity covering the header and complete payload, including unknown records,
   with the checksum field's treatment specified. Define limits and validate
   lengths, counts, offsets, nesting, references and arithmetic before allocation
   or execution. Corruption or insufficient resources produces an explicit error.
5. Deterministic encoding and shared Python/C++ byte fixtures. Native alignment
   and compiler structure layouts never define the wire format.

Use the required flag as the persistent compatibility mechanism. A record is
required if ignoring it can change execution or the interpretation of other
records. New actions, gestures, processors and behavior-changing settings are
required. Pure descriptive metadata may be optional, including its entire subtree;
an optional parent cannot hide required descendants. The writer marks records
correctly, and readers reject unknown required records anywhere in the relevant
document and its dependencies before activation or MIDI output.

An implementation also rejects unsupported values in understood records, such
as a new asset codec. Feature support reported to the host is derived from the
same registry and implementation. Do not store a second capability list that can
disagree with the records. f0 readers must enforce these rules from the start.

Unknown optional records must survive read/edit/save, including inside understood
parents. Preserve their bytes and meaningful ordering. If an editor cannot retain
them, refuse that save while allowing backup. Optional records must not affect
execution or be the sole owner of executable references.

For structural edits such as rename, deletion or reindexing, reject the edit when
an opaque record in the affected scope might contain references it would break.
No general reference-rewriting engine is required. Unrelated supported edits
remain available when preservation is safe. A converter follows the same rule.

Compatible additions stay within f0. A new tag can express new behavior without
changing old tags or absence defaults. Changed existing meanings, units or record
encoding require the next released format unless a distinct new tag fully
preserves the old contract. Do not version individual fields or bump f0 for a
runtime-only layout change.

## Released-format converters

Implement host-side functions for adjacent released formats. Each reads a frozen
source schema and writes the frozen next schema, independently of current model
defaults. Keep the functions and fixtures permanently. For f0 data opened by f3
software, run f0 to f1, f1 to f2, then f2 to f3. Do not implement future converters
until their breaking changes exist.

Conversion covers the inventory and all nested dependencies, including history,
captured song setups and synchronization baselines. Files whose encodings are
unchanged are copied unchanged. Recompute derived tables, references, integrity
checks and dependent hashes together. Preserve musical ordering, timing, labels,
identities, edits and valid undo content.

Write deterministic output to a separate destination and a small TOML report
identifying input/output paths, formats, digests and failures. Preserve originals.
Unknown optional records survive when the new encoding can retain their meaning.
Otherwise fail with the affected path and reason. Never drop data, approximate an
operation or reset a value silently. Converter fixes require regression fixtures.

Before f0, inventory the development formats actually present in retained backups
and deployed devices, then provide and document the needed importers. There is
no promise to support every development build. Development importers are separate
from the permanent released-format chain. Never relabel legacy bytes as f0.

## Recovery and deployment

Use the existing backup, synchronization and deployment tools. Assume host edits
and pedal edits do not run concurrently; suspend editing during conversion and
deployment. No distributed transaction or new backup subsystem is needed.

1. Discover the intended firmware's format and feature support. Enumerate all
   persistent content and references, recover any pending synchronization apply,
   then verify an original backup and a matching firmware recovery procedure.
   Keep corrupt bytes and report missing content. Do not claim complete conversion
   while required content is missing or unconvertible.
2. Convert into a separate host destination. Validate every document, reference
   and asset against the intended firmware, and check peak RAM and storage needs.
   Space for staging and recovery must be included; insufficient space stops the
   upgrade before overwriting active data.
3. Stage changed device files without overwriting the committed set. Reuse
   unchanged assets. Read back and validate staged content. Use existing slot
   journals where they suffice. For a multi-file change, use one small journaled
   activation record identifying the complete staged set; partial sets must never
   become active. Conversion digests verify the initial transfer. Later saves
   advance records within the selected set using their normal journals.
4. Follow a documented adapter-specific sequence for firmware flashing and data
   activation. These are separate physical operations. At startup, validate the
   selected set and firmware compatibility before executing it. An incomplete or
   incompatible upgrade enters recovery instead of running mixed data or
   automatically replaying uncertain operations.
5. Verify startup, setup selection, history restore, song captures, assets and a
   pedal edit/save/reload. Update host baselines after successful activation.
   Make an interrupted baseline update safely resumable using the conversion
   report and existing synchronization journal. Retain the original backup and
   old recovery pair until the upgrade is accepted.

Single-file saves continue to use their existing journals. The activation record
is only needed for changes that must commit together. Define flush and commit
selection guarantees for each adapter; do not assume rename alone is safe under
power loss. Do not rewrite untouched historical originals on every upgrade.

An interrupted data-only stage leaves the committed data selectable. An interrupted
flash may require host-assisted reflashing; uninterrupted operation is not
promised. The guarantee is recoverable original content and a tested route back
to a compatible firmware/data pair. Both automatic and host-assisted recovery
must identify which complete set is selected and must never activate a mixture.

Rollback preserves any new recordings and counter high-water mark. Back up new
edits before restoring older data, since old firmware may not understand them.
Original history bytes remain exportable from the retained backup; converted
history remains available on the upgraded device without requiring duplicate
archival copies there.

## Implementation and release gates

1. Confirm the inventory and committed-record precedence. Collect real legacy
   samples and document the development importers needed for f0.
2. Specify the envelope, record registry, defaults and references. Implement host
   and firmware codecs with shared byte fixtures and unknown-record preservation.
3. Update saves, history, songs, backups and synchronization consistently. Add
   importers and the minimal host converter dispatch for released formats.
4. Integrate staged deployment and adapter recovery with existing tools. Measure
   file size, load time, edit/save cost and peak memory on the current device.
5. Pass the checks below, then tag the public release with its f0 specification
   and fixtures. Retain released fixtures and add converters as formats evolve.

Required checks are:

- Python and C++ agree on bytes, defaults and decoded meaning for each artifact
  kind. Unknown optional records survive nested edits; unknown required records
  and unsupported values prevent activation. Unsafe structural edits are refused.
- Invalid lengths, counts, nesting, references, text and checksums fail before
  allocation or execution, leaving original files intact.
- Every released converter chain preserves behavior and metadata. Fixtures cover
  reordered setup tables, history reservations, captured setups, asset keys,
  missing sources, undo and synchronization recovery state.
- Interrupt writes, transfer, activation and firmware flashing at durable
  boundaries. Demonstrate complete-set selection and automatic or host-assisted
  recovery, including new recordings and recording-name uniqueness after rollback.
- Round-trip real pre-release backups through f0, pedal edits, backup, history
  restore and source synchronization. Verify MIDI timing, ordering, bindings and
  inherited patch behavior. Host tests alone do not qualify physical recovery.

## Additional work beyond the prompt

None.

## Implementation status

The software implementation uses development format **d45**, not released f0.
All three artifact kinds (setup, bundle and song library) use the same envelope
and required-record rules. The byte specification, container layouts, journal
rules, development inputs and operational recovery protocol are in
[`doc/binary-format.md`](../doc/binary-format.md). Shared setup, bundle and song
fixtures live under `firmware/test/` and are regenerated by the tracked fixture
script. Python and C++ tests cover optional metadata, unknown required records,
unsafe structural edits, streamed import, numeric saves, history, undo across
reboot, captured reference reindexing, baseline recovery, torn activation writes
and recording counter high-water marks.

No future released-format converter is implemented because no adjacent released
pair exists. The retained development inputs have explicit import paths; their
original bytes are kept. f0 will freeze the schema and fixtures at its release
commit, independently of later model defaults.

The remaining release gates are physical qualification: real retained-backup
round trips on the pedal, MIDI timing and edit/save measurements, SD power-loss
tests, and interrupted-flash recovery with a retained matching image. Firmware
builds and linked RAM measurements can run without flashing. These physical
gates are intentionally deferred while the user has prohibited flashing; they
must pass before a public f0 tag is created.
