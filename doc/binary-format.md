# Persistent binary formats

The implementation is currently **development format d45**, encoded as `0x802d`.
It is not a release of f0. The high bit distinguishes development formats from
released formats; f0 will have the numeric value zero. Change the development
number when an incompatible development encoding is deployed or retained for use.
The [release plan](../plan/binary-format.md) defines the permanent promise that
begins at f0 and the physical qualification required before freezing it.

## Envelope

All integers below are unsigned little-endian values. No native structure layout
defines these bytes. The current header is exactly 20 bytes.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `CNBF` |
| 4 | 2 | Format identifier, currently `0x802d` |
| 6 | 2 | Artifact kind |
| 8 | 2 | Header length, currently 20 |
| 10 | 2 | Reserved flags, zero |
| 12 | 4 | Payload length, excluding the header |
| 16 | 4 | CRC-32 of the entire document, treating these four checksum bytes as zero |

CRC-32 is the reflected IEEE CRC used by `zlib.crc32`, with polynomial
`0xedb88320`, initial state `0xffffffff` and final XOR `0xffffffff`.
Header fields, record headers and opaque records are covered. A checksum detects
corruption and is not an authentication mechanism.

The payload consists of records with this eight-byte prefix:

| Relative offset | Size | Meaning |
| --- | --- | --- |
| 0 | 2 | Permanent nonzero tag |
| 2 | 2 | Flags; bit 0 means required, all other bits are zero |
| 4 | 4 | Record payload length |
| 8 | variable | Exactly that many payload bytes |

Limits are 16 MiB of payload and 4,096 records. Device resource limits are
separate and usually smaller. Reject truncated records, trailing bytes that
cannot form a record, unknown flags and inconsistent lengths before allocation
or execution. Unsupported format identifiers are rejected rather than inferred.

## Registry and editing rules

| Identifier | Meaning |
| --- | --- |
| Kind 1 | Setup document |
| Kind 2 | Indexed setup bundle, with a `CSTB` version-1 core |
| Kind 3 | Song library, with the counts/records/`CSP5` core described below |
| Tag 1 | Required, first, singleton core snapshot |
| Other tags | Unrecognized optional metadata can be retained; unrecognized required semantics prevent activation |

The setup core snapshot uses the existing explicitly encoded `CSW1` schema 45,
including its indexed graph, fixed settings and retained source metadata. It is
one record inside the authoritative document, not a separately editable cache.
The inner body checksum and length are retained. Numeric edits update both the
core and envelope checksums. Existing bare setup documents are development
import inputs, not released f0 files.

The core's offsets, operation encodings and units are described by the
[setup wire layout](../firmware/README.md#binary-and-transfer-protocol) and the checked-in
fixtures under `firmware/test/`. The fixtures are generated from tracked inputs
with `uv run scripts/regenerate_fixtures.py`. The normative f0 schema and fixtures
must be frozen together at release, independently of future model defaults.

Unknown optional records are preserved byte for byte, in their original order,
through host memory-accounting updates, pedal numeric edits/save/history,
bundle saves, song import/undo and backup conversion.
They may contain opaque nested descriptive metadata. An optional subtree cannot
introduce executable semantics. Known records must also reject unsupported
values, such as an unrecognized operation or asset codec.

An editor refuses transformations that could invalidate opaque references.
This includes remapping a setup table, changing node identities or upgrading a
core layout it does not understand. Ordinary numeric edits retain the graph and
its indices. A failed edit or conversion leaves the original bytes available.

Within a released format, tags and absence defaults never change meaning and
retired identifiers are never reused. New behavior uses new required records.
An incompatible encoding or meaning requires the next released format. Host
converters for every adjacent released pair will remain available permanently;
there are no released pairs yet, so no speculative f0-to-f1 converter exists.

## Existing persistent containers

The authoritative bundle and song payloads are complete envelopes, not detached
metadata or rebuilt portable caches. Their indexed cores remain inside tag 1.
Offsets in these cores are relative to the core, not the outer envelope. Optional
records follow the core and never become part of its indexed data.

The bundle core is `CSTB` version 1. Its 40-byte header has magic at 0, u16 version
at 4, u16 nonzero setup count at 6, then eight u32 values: core length, CRC-32 of
bytes after this header, memory margin, runtime RAM, maximum active-setup RAM,
non-setup RAM needs, total setup payload RAM, and free setup RAM.
Each setup entry is 76 bytes: a zero-padded ASCII name[64], u32 document length,
u32 active RAM estimate, u8 prefill flag (0 or 1), and three zero bytes.
Entries are sorted by ASCII case folding. Names contain 1..63 filename-safe
printable ASCII characters. The table after the entries contains a u16 count and
count-minus-one u32 cumulative document offsets, followed by the setup envelopes.
Setup zero starts at offset zero within that final document slab.
An offset is meaningful only with its own document and saved name table.

The song core begins with u32 song and setlist counts. Each song has one
200-byte selection (document[64], patch key[64], UTF-8 display label[64], u32
captured-source index, u32 graph-node index), followed by name[64], u32 nonzero
selection count, then the remaining selections. Each setlist has name[64], u32
entry count, and that many song names[64]. All fixed text fields are zero padded
and terminated. Names and external document/key references use the existing
ASCII name rules; display labels are UTF-8. The four bytes `CSP5` follow the
setlists. Captured sources then repeat as u32 byte length plus the complete setup
document, until the end of the core. External document selections use source
index `0xffffffff`; captured source and node indices are zero based.
Order, repetitions and node identities are significant.

Bundle numeric saves retain outer metadata. Deleting or reindexing entries with
opaque metadata is refused. Song structural edits with opaque library metadata
are similarly refused, while choosing songs, exact import, download and undo
remain available. Converters preserve safe memory-estimate changes and refuse
changes to lengths or reference tables that opaque records could depend on.

Both containers are validated using bounded streaming reads. A validation step
reads at most 512 bytes without allocating an entire bundle or library. Song
publication hashes the complete envelope, writes only an inactive journal slot,
then independently checks its bytes, records and core before selecting it.

New song journal files use `CSG6`: magic[4], u32 generation, u32 envelope length,
u32 envelope CRC-32, u32 undo generation (zero means no undo), u32 CRC-32 of the
preceding 20 header bytes, the envelope, then `DONE`. Thus undo availability and
its consumed state survive reboot. The newest integrity-valid generation must
also be understood; unsupported required records cause recovery, not fallback
to an older musical collection. Torn or corrupt uncommitted records may fall
back to the previous complete record.

`CSG5` journals and bare bundle/library cores remain development import inputs.
d44 setup envelopes and activation records remain readable; new writes use d45.
d45 prevents d44 firmware from silently selecting an older library after a
new-format library save: after validating startup stores, the adapter publishes
a d45 activation barrier before enabling ordinary runtime saves. A failed store
validation or barrier write enters recovery instead of enabling musical input.
Setup journals use `CNST`, history uses `CREV`, and
selection uses `CSEL`. The newest committed bundle is authoritative; raw older
per-setup journals are retained as recovery material rather than merged as
competing overrides. Existing asset codecs `CFN2`,
`CNSA` v2, Standard MIDI Files and TOML records retain their encodings. An
envelope does not imply that firmware supports an asset's codec.

Development setup importers cover 36, 38, 39, 40, 41, 42, 43, 44 and 45. Retired development
archives remain raw recovery material unless an importer is explicitly added.
Unsupported or ambiguous input fails conversion; it is never relabeled as f0.

## Offline conversion and backups

Use the existing setup utilities:

```sh
cntrl setup backup original --port DEVICE
cntrl setup convert original --destination converted --target target.toml
cntrl setup restore converted --port DEVICE --target target.toml
cntrl setup verify converted --port DEVICE --target target.toml
```

Conversion requires a new destination outside the original backup. It converts
current setups, the complete bundle, history, active/undo libraries and captured
song sources without recompiling musical
operations from current authoring defaults. Assets and original raw journals are
copied unchanged. `conversion.toml` records digests of every resulting file.
New backups retain `bundle.bin` alongside individual authoritative setup files;
conversion refuses disagreement between them. Restore relinks captured setup
references against the final merged setup table. Upload synchronization retains
the original container in `bundle.before` without changing authoring inputs.
If the new firmware cannot read the active collection, restore accepts a verified
offline conversion, retains a digest-checked raw recovery backup and the original
device root, and stages the converted collection directly. It does not attempt
to merge uninterpretable active documents or claim that they were converted.
An unconverted backup cannot enter this recovery-install path.
An interrupted conversion retains `conversion-pending.toml`; restoration refuses
that directory. Backup manifests similarly distinguish incomplete captures from
verified complete captures. Changed manifest-covered files are rejected.

Backups preserve all stored MIDI recordings, the startup counter where its
protocol is available, and raw active journal/adapter files on current firmware.
Raw files are under `raw/` with their device paths, so unsupported or damaged
formats can still be recovered. Calibration and firmware-return bytes are
archived without automatically restoring them onto another physical device.
Earlier firmware may provide only the older structured backup protocol, which
is recorded as `raw_state = false` in the backup manifest.

Source TOML composition and ownership remain intact. Synchronization snapshots,
baselines and pending recovery journals are persistent project data. Resolve an
interrupted synchronization apply before migrating it. A changed baseline
invalidates older unapplied reviews; their choices remain available for inspection.

## Coordinated deployment and recovery

Single-file saves continue using their journals. For a coordinated restoration,
the host first retains a recovery backup, stages the complete mutable collection
and reads it back. Only then does it publish one activation record. Setup/history,
selection and song paths map into `/cntrl/.generations/<eight-hex-root>/`.
Root zero refers to the original paths. Retained roots are never overwritten.

Fonts, movies, recordings and the startup counter remain shared. Immutable assets
are reused; a MIDI filename collision with different bytes is refused. The
startup counter must never be restored to a lower value over surviving recordings.

The activation journal uses `/cntrl/.deploy/0.bin` and `1.bin`. Each record is:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `CDEP` |
| 4 | 4 | Nonzero monotonically increasing sequence |
| 8 | 4 | Selected root identifier |
| 12 | 2 | Required persistent format |
| 14 | 2 | Required stores: bit 0 bundle, bit 1 library; other bits zero (d44 used zero) |
| 16 | 4 | CRC-32 of bytes 0 through 15 |
| 20 | 4 | ASCII `DONE` |

The highest valid sequence selects the root. Sequence exhaustion is an error;
it never wraps. A newer committed unsupported format enters recovery rather
than silently choosing older data. The initial legacy root is journaled before
staging a replacement. Each activation write is flushed and read-back verified.
Required-store flags prevent missing journals from being initialized as an empty
collection. Positive generation roots must have their completion marker; library
initialization is allowed only for a fresh stage or an initial legacy root.
Later ordinary saves advance their own journals within the selected root.
Before selecting a new root, publication also retains a flushed `.committed`
copy of its activation record inside that root's generation directory. Explicit
recovery requires this proof of a completed stage, so an abandoned partial
directory cannot be mistaken for a known-good retained collection.

The protocol provides `CNTRL DEPLOY STATUS`, `BEGIN <hex-root>`,
`COMMIT <bundle-crc> <song-crc>`, `ABORT` and `RECOVER <hex-root>`. Device
runtime input, expression and musical service are suspended during staging.
Commit checks the uploaded bundle and song payload digests. A reconnecting
restorer aborts an unfinished staging session before taking another snapshot.
`CNTRL FORMAT` advertises the current format and the supported artifact kinds
(`1 2 3`). The host requires the kind it will upload before sending those bytes.
USB command replies continue to be serviced during staging; musical input is
discarded, so it cannot be replayed against the newly selected collection.

Select an explicitly known-good retained root with:

```sh
cntrl setup recover --port DEVICE --root ROOT_NUMBER
```

Recovery validates that root's setup bundle and song library against the running
firmware before committing its selection. Read-only `CNTRL STATE SIZE <hex-path>`
and `READ <hex-path> <offset>` allow recovery downloads beneath `/cntrl/` without
understanding their format. Traversal outside that directory is rejected.

An interrupted data stage leaves the committed root available. An interrupted
firmware flash may require host-assisted reflashing of the known-good image.
Software checkpoint tests do not establish SD-card power-loss behavior or
uninterrupted operation during flashing. Those remain physical release gates.
Retain the original host version and complete known-good firmware build output
with the backup. If that older firmware cannot understand the current activation
format, recovery uses the original raw mutable stores and activation journals
with that matching image, rather than guessing a downgrade. Recordings and their
counter high-water mark must be preserved separately throughout rollback.
