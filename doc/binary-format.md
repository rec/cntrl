# Persistent binary formats

The implementation is currently **development format d44**, encoded as `0x802c`.
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
| 4 | 2 | Format identifier, currently `0x802c` |
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
| Kind 2 | Reserved for an enveloped setup bundle |
| Kind 3 | Reserved for an enveloped song library |
| Tag 1 | Required, first, singleton core snapshot |
| Other tags | Unrecognized optional metadata can be retained; unrecognized required semantics prevent activation |

The setup core snapshot uses the existing explicitly encoded `CSW1` schema 43,
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
through host memory-accounting updates and pedal numeric edits/save/history.
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

The current bundle codec is `CSTB` version 1, containing a sorted, case-insensitive
ASCII name table, setup lengths, RAM estimates and indexed setup bytes. Each
newly compiled setup is enveloped. Runtime setup indices are meaningful only
with the saved table or signature that produced them.

Current song payloads contain `CSP5` source metadata and may capture complete
enveloped setups. Song journal files use `CSG5`. Setup journals use `CNST`,
history uses `CREV`, and selection uses `CSEL`. Existing asset codecs `CFN2`,
`CNSA` v2, Standard MIDI Files and TOML records retain their encodings. An
envelope does not imply that firmware supports an asset's codec.

Development setup importers cover 36, 38, 39, 40, 41, 42 and 43. Retired development
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
current setups, history and captured song sources without recompiling musical
operations from current authoring defaults. Assets and original raw journals are
copied unchanged. `conversion.toml` records digests of every resulting file.
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
| 14 | 2 | Reserved, zero |
| 16 | 4 | CRC-32 of bytes 0 through 15 |
| 20 | 4 | ASCII `DONE` |

The highest valid sequence selects the root. Sequence exhaustion is an error;
it never wraps. A newer committed unsupported format enters recovery rather
than silently choosing older data. The initial legacy root is journaled before
staging a replacement. Each activation write is flushed and read-back verified.
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
