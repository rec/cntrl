# Public Morningstar backup research corpus

Collected for [the decoding and conversion plan](../../../../../plan/exporter.md).
There are 19 native backups (11 MC6 Pro, six non-Pro MC8, two MC8 Pro), one
supplementary FootSmart authoring file, 73 MC8 Pro SysEx captures and two licenses.

`sources.toml` pins upstream commits and file selection. `manifest.toml` records
original paths, source profiles, sizes, SHA-256 hashes and Git blob hashes.
Original JSON bytes are compressed as `.backup.gz`, binary captures as
`.sysex.gz`; decompression reproduces the original bytes without reformatting.
`footsmart/old/NavigatorTest_copy.backup.gz` is an authoring example rather than
a native backup. Its inventory entry explicitly distinguishes that format.

FootSmart is MIT-licensed and morningstar-sdk is Apache-2.0-licensed; their
licenses are retained in their source directories. No license was declared for
jekku/mc6-pro-collection or bwanab/MC8_setups at collection time. Public access
is not a redistribution grant. See the pinned repository links in the plan.

From the repository root, with the project's declared development dependencies
installed, regenerate the offline evidence index with:

```sh
.venv/bin/python cntrl/maker/morningstar/mc/backup_inventory.py
```

This verifies every decompressed file against its recorded size and SHA-256
before rewriting `inventory.toml`. Windows can use `.venv\Scripts\python.exe`
with the same script path. Recollect the pinned source bytes, when intentionally
requested, by adding `--collect`; collection requires GitHub CLI access and
overwrites the corpus files and manifest. Collection and inventory are separate
steps. No converter, device command or upload is performed.

The inventory counts only `t != 0` slots as encoded messages. It keeps bank,
preset and expression namespaces separate and records trigger, toggle-position,
channel, payload-layout and raw-payload-variant counts. Message counts do not
establish reachability or execution. `first_pointer` addresses a representative
slot in the decompressed source, and `first_payload` is its raw payload encoded
as text within TOML. `message_slots` includes empty slots; `bank_count` includes
empty banks. `body_sha256` hashes canonical JSON serialization of the `data`
object, for exact body equality only. Controller settings and palette fields are
listed structurally, without pretending their contents have been decoded.

The tracked script is a research collector and inventory generator, not a new
public `cntrl` command. Future user-facing conversion belongs in the existing CLI.
