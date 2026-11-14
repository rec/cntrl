# Decode Morningstar backups and convert them to cntrl documents

## Goal and present status

Import stock Morningstar configurations into editable cntrl TOML, preserving
the MIDI output and control behavior that cntrl can express. This is a plan
for later implementation, not an implemented converter. Despite this document's
requested name, the direction is Morningstar backup to cntrl authoring files.
Writing stock-firmware backups, uploading, and flashing are outside its scope.

The public examples have been collected under
[backup_examples](../cntrl/maker/morningstar/mc/backup_examples/README.md).
The collection contains **19 native JSON backups, one supplementary FootSmart
authoring example, 73 binary SysEx captures and two upstream licenses**.
Original bytes are gzip-compressed, without JSON reserialization, in files
ending in `.backup.gz` or `.sysex.gz`. The collection occupies about 1.7 MiB
including its metadata; original files total 143,176,885 bytes.

[sources.toml](../cntrl/maker/morningstar/mc/backup_examples/sources.toml)
pins repositories and revisions.
[manifest.toml](../cntrl/maker/morningstar/mc/backup_examples/manifest.toml)
records each original filename, local path, byte count, SHA-256 and Git blob.
[inventory.toml](../cntrl/maker/morningstar/mc/backup_examples/inventory.toml)
records per-file message counts, trigger/toggle/channel codes, payload layouts
and distinct payload counts, representative JSON pointers, field names and
selected flags. It is reproducible offline using the tracked inventory script.

## Sources and model boundaries

| Source, pinned revision | Collected examples | Role and licensing |
| --- | --- | --- |
| [jekku/mc6-pro-collection](https://github.com/jekku/mc6-pro-collection/tree/c0807f26079b1c129c04ff47b9ccab4579df3e76), `c0807f2` | One MC6 Pro all-banks backup, 128 bank records | Real configuration; no license declared at collection time |
| [bwanab/MC8_setups](https://github.com/bwanab/MC8_setups/tree/d4f7e4a340dd210e3b7cc3b3ce483384595971a0), `d4f7e4a` | Three non-Pro MC8 all-banks snapshots, 30 bank records each; three single-bank exports | Kemper Performance Select, Logic Live Loops and Mainstage FX; no license declared at collection time |
| [jgateley/FootSmart](https://github.com/jgateley/FootSmart/tree/c1245fe93f50625063e9782dc1ae9029b2f437b9), `c1245fe` | Nine MC6 Pro `Configs/Test` backups, historical `SelectExpMessage` backup, supplementary `NavigatorTest_copy` authoring example | Focused feature and payload variants; MIT license included |
| [benemon/morningstar-sdk](https://github.com/benemon/morningstar-sdk/tree/38b271ebfa259f59118442f0983ee223c068a1fc), `38b271e` | MC8 Pro all-banks and Guitar Live single-bank backups; 73 raw SysEx captures | Paired configuration/protocol evidence; Apache-2.0 license included |

Public availability does not itself grant redistribution rights. The first two
sources have no declared license; retain that fact and resolve permission before
shipping their contents in a distributed product. Keep third-party examples and
their licenses distinct from our implementation.

Use **6 Pro**, **8**, and **8 Pro** as separate source profiles. Observed native
headers use `deviceModel = 6`, `4`, and `8`, respectively. In particular, the
non-Pro MC8 does not use model code 8. All 19 native backups declare
`schemaVersion = 1`; that does not establish shared payload semantics or firmware
compatibility. Preserve source firmware/editor versions where known and record
unknown versions honestly. The SDK describes hardware testing on MC8 Pro
firmware 3.13.6; that is its author's evidence, not our hardware qualification.

`NavigatorTest_copy` has a FootSmart authoring schema, not a Morningstar backup
header. It is supplementary reference material and must never count as native
format or feature coverage. The user's existing
[6 Pro backup](../cntrl/maker/morningstar/mc/Morningstar_MC6PRO_All_Banks_Backup_20261021_135556.json)
remains an important additional acceptance case, but is deliberately not our
only specification or duplicated in this public collection.

## Observed feature coverage

Coverage here means a non-empty encoded slot (`t != 0`), not proof that a message
executes. Disabled triggers, unreachable banks and host-specific assignments
need separate analysis. Default empty slots can contain nonzero payload bytes.
Bank counts include empty padded banks; repeated snapshots are not independent
feature evidence. Body hashes in the inventory identify exact equality of the
`data` object, not behavioral equivalence.

Message names below are interpretations from the pinned
[FootSmart decoder](https://github.com/jgateley/FootSmart/blob/c1245fe93f50625063e9782dc1ae9029b2f437b9/simple_message.py)
and [SDK enum definitions](https://github.com/benemon/morningstar-sdk/blob/38b271ebfa259f59118442f0983ee223c068a1fc/pkg/mc8pro/model/enums.go).
They are starting hypotheses to verify against the editor and stock firmware,
especially for bank messages and the legacy non-Pro format.

| Example group | Encoded features present | Important limits |
| --- | --- | --- |
| 6 Pro, jekku | Preset CC `2` (52 slots), bank jump `13` (2), toggle page `14` (2), toggle preset `22` (10); bank types `1` and `15` (one each) | Press/release/long-press codes; positions 1/2/both. No expression messages |
| 6 Pro, FootSmart `Features` | PC, CC, note on/off, realtime, SysEx, MIDI clock/menu, bank up/down/change/jump, page change, set toggle, MIDI thru, looper mode, toggle preset, delay, clock tap, song position, waveform, engage preset, sequencer, CC/PC scrolling, multichannel PC, utility, relay, MMC and rename | 29 preset type codes and bank PC. `%G` display token and two message-scroll flags. Not all submodes are established by the fixture's name |
| 6 Pro, `MIDIClock` | Type `7`, 21 slots with clock/menu payload variants | Timing and start/stop semantics still need verification |
| 6 Pro, `PCNumberScroll` | CC scroll `32`, 11 slots; PC scroll `33`, 13 slots | Counter selection, wrap, update/send/reset and bounds need payload decoding |
| 6 Pro, `SetToggle` | Type `15`, nine slots | Cross-preset selection and toggle-group behavior need verification |
| 6 Pro, `Utility` | Type `36`, 33 slots | Inventory counts raw variants; utility sub-opcodes are not yet decoded |
| 6 Pro, `WaveformSequence` | Waveform `26`, 32 slots; sequence `31`, eight slots | Includes action codes 1, 2, 3, 4; engine start/stop, synchronization and continuation are separate semantics |
| 6 Pro, `ExtraBytes` | Preset PC; bank types `6` and `18`, candidate set-toggle and clock under FootSmart's bank table | Useful for trailing bytes that a simplistic decoder might discard |
| 6 Pro, `Empty` and `NewFields` | No non-empty messages | Structural/default-field fixtures, not evidence of working features |
| 6 Pro, historical `SelectExpMessage` | Select-expression-message `17`, four slots; note-off `4`, 93 slots | Selection commands exist, but every expression-message slot is empty |
| Non-Pro 8, all-banks snapshots | PC `1` in all three; CC `2` in the latter two; press, long-press and double-tap action codes | Separate `d1`–`d4` payload fields. No bank or expression messages |
| Non-Pro 8, Kemper single bank | Five PC and 25 CC slots; eight toggle-enabled presets; positions 1/2/both | The author's README supplies intended device behavior; that is not a decoded device-integration message type |
| Non-Pro 8, Live Loops single bank | Eleven CC slots; press/double-tap codes | Track/scene/cell meanings depend on matching Logic mappings |
| Non-Pro 8, Mainstage FX single bank | Fifteen CC slots; eight toggle-enabled presets; includes action code 0 | Labels describe effects, tuner and patch selection, implemented as host-mapped CCs |
| 8 Pro, both JSON backups | Eight CC, four set-toggle and one MIDI-clock-tap slot per export; five toggle-enabled presets; press/long-press codes | The single-bank export repeats the Guitar Live configuration. Only three preset type codes; no non-empty bank or expression messages |
| 8 Pro, 73 SysEx captures | Binary frames, lengths and prefixes indexed separately | Protocol research evidence; not 73 additional patch configurations or evidence for every SDK enum |

Across the 6 Pro native examples there are 30 distinct non-empty preset type
codes: `1–7, 10–18, 22–27, 31–34, 36, 40, 41, 44`. Bank codes are
`1, 6, 15, 18`, a different namespace until proved otherwise. The native
non-Pro 8 examples contain preset codes `1, 2`; the 8 Pro examples contain
`2, 15, 24`. No profile has non-empty expression messages. No collected native
message uses shift position `tg = 3`, and no preset has `toBlink = true`.

Controller-settings blobs, bank/preset labels and, on Pro models, color/page
fields are present. Their existence or default values do not establish routing,
calibration, page, palette or device-global behavior. The inventory lists their
field names without claiming to decode the settings blob.

## Decoding approach

1. **Read the native envelope and preserve evidence.** Start from the exact input
   bytes, source profile, header, bank numbers, preset numbers and ordered slots.
   Record file and JSON pointer for diagnostics. Distinguish `allBanks` from
   `singleBank`, and controller settings from bank-local data. A single bank may
   depend on omitted banks, global counters or routing; report those dependencies
   rather than synthesizing them. No Morningstar hash algorithm is needed for
   one-way import; retain its value without claiming verification of it.
2. **Document layouts before normalizing them.** Decode legacy `d1`–`d4` and Pro
   `data` arrays independently. Preserve unused bytes while researching bitfields,
   enums, masks and sub-opcodes. Do not pad legacy fields into an assumed Pro
   layout. Treat differences between absent, zero and default values explicitly.
3. **Keep namespaces and state separate.** Decode `t` in bank, preset and
   expression context. FootSmart's bank enum differs from its preset enum;
   the SDK claims a shared preset/bank enum on 8 Pro. Resolve that disagreement
   with model-specific evidence. Decode `a` by context, including bank enter/exit
   and once-only behavior. `tg` selects a message's toggle position; it is not
   the preset's separate `toggleGroup`. Preserve message order, gaps and indices
   because other commands can refer to individual message slots.
4. **Decode basic output, then stateful commands.** Establish channels, PC
   numbering, CC values, note values/velocity, realtime, SysEx lengths, delay
   units and output-port masks first. Then follow cross-bank/preset references,
   page numbering, toggle operations, counters and generator lifecycles. Do not
   assume that sending a PC creates a cntrl instrument definition or that a
   stock bank is necessarily a cntrl MIDI bank-select value.
5. **Fill gaps with controlled evidence.** For each uncertain field, export an
   empty baseline and variants changing one editor setting. Record exact editor,
   firmware and model versions. Cover expression endpoints/curves, shift,
   release-all/first-engage/disengage, blink, message scrolling, routing/global
   settings, engine persistence and reset behavior. Use both limits and ordinary
   values to distinguish bitfields from scalar bytes. Add only gap-driven
   fixtures; a named feature in someone else's file is not a complete test matrix.

For every decoded feature, retain a small evidence table of byte/bit meaning,
units, enum values, profile/context, source pointers and confidence. Reference
third-party decoder implementations; do not assume they are authoritative or
copy their architecture wholesale. Unknown fields/submodes remain visible in
the decoding report. We must not silently discard potentially meaningful data.

## Conversion into the existing cntrl format

Use [existing models](../cntrl/patch_switcher/models.py),
[authoring loader](../cntrl/patch_switcher/data_files.py),
[compiler](../cntrl/patch_switcher/compiler.py) and
[editable exporter](../cntrl/patch_switcher/resolved.py).
Manufacturer-specific decoding belongs under `cntrl/maker/morningstar/mc/`.
Do not create a second configuration language, compiler or firmware interpreter.

| Source behavior | Candidate existing cntrl representation | Evidence required before declaring a faithful conversion |
| --- | --- | --- |
| Ordered MIDI messages and delays | `Sequence`, channel PC, CC, `Note`, `SystemMessage`, `SysEx`, `Delay`; MMC as its verified SysEx bytes | Byte order, channel/port routing, units, PC 0–127 versus cntrl authoring 1–128, note-off representation and exact emitted bytes |
| Physical switch and gesture assignments | `SwitchAssignment`, `SwitchBinding`, `SwitchGestures` | Explicit preset-to-button/page mapping; long/double/release event suppression and timing, not just matching event names |
| Bank entry/exit and navigation | Setup/page-local bindings, `enter`/`exit`, `Navigation`, named references where semantics match | Decide bank-to-setup/page organization after decoding dependencies; prove transitions, once-only actions and scope match |
| Toggle and shift behavior | Latched bindings, `Shift`, shifted actions where equivalent | Stock per-preset toggle/group state and external set-toggle commands may exceed current semantics; never map toggle to the shared shift layer simply because both have two values |
| PC/CC scrolling | Existing `Counter` and `ScrollCounter` | Initial values, bounds, wrap, shared counter scope, send/update/reset ordering and bank-change persistence |
| Clock, waveform and sequence engines | Existing clock streams, LFOs and supported engine controls | Waveform shapes, discrete steps, phase, clock lock, start/stop and background continuation; name similarity is insufficient |
| Expression | `ExpressionAssignment` for matching continuous CC mappings | Range/direction/curve, routing and activation rules; toe/heel, PC, pitch-bend and engine-speed subtypes require separate support assessment |
| Names, palette and dynamic labels | Existing presentation/style/page records for expressible values | Decode palette indices and macros such as `%G` or the user's `%F3`; do not silently freeze dynamic text |
| Relay/thru/utility/looper/device integration | Existing operations only where their semantics are demonstrated | Distinguish stock looper mode from cntrl MIDI recording; distinguish external device CC assignments from manufacturer integration commands |

Do not infer one universal bank-to-setup mapping now. First decode a real bank
and its references, then choose an organization that can be expressed by current
models. Stock message scrolling is not automatically patch cycling, and stock
page changes are not automatically cntrl utility-page navigation.

The converter should produce a reviewable report before writing new authoring
documents. List supported behavior, unresolved references, unsupported operations
and affected source pointers. Write complete converted documents only when the
selected scope has no unresolved behavioral loss. A research decode report may
still be useful for unsupported inputs. Do not create no-op patches, raw opaque
payload fallbacks or a second runtime to make an import appear successful.

Write into a new output directory without overwriting source documents. Derive
readable names deterministically and handle empty/duplicate bank or preset labels
without losing references. Keep source coordinates in the conversion report;
they need not become persistent IDs in cntrl's authoring schema. Require an
explicit destination target and output routing when source settings cannot
determine them. Compile generated TOML with the current compiler before reporting
success. Any required new cntrl runtime semantics need a separate proposal and
approval, rather than being hidden inside the importer.

## Proposed implementation commits and acceptance

1. **Native envelope and decoded inspection.** Parse these three observed profiles,
   enumerate unsupported structures, retain source coordinates and test all 19
   native inputs. Recognize the supplementary FootSmart file as a different
   format. Publish a human-readable decoded inspection without device access.
2. **Basic messages and one stateless bank.** Decode PC/CC/notes/realtime/SysEx/
   delay, routing and ordinary gestures; emit current cntrl models and TOML.
   Use a limited real-world bank with independently verified MIDI output as the
   first end-to-end case. Do not mark the richer fixtures as fully converted.
3. **Navigation, references and state.** Resolve cross-bank/preset dependencies;
   add only mappings proven equivalent for toggles, counters and message
   selection. Report unrepresentable behavior and propose any runtime work
   separately. Add focused fixtures for gaps discovered in this phase.
4. **Advanced engines, expression and presentation.** Decode remaining payload
   families and globals, implement supported mappings and explicitly document
   limits per model. The richer 6 Pro fixtures guide decoding; acquire additional
   8/8 Pro examples before claiming equivalent feature coverage on those profiles.
5. **CLI and user workflow.** Expose the completed importer through the existing
   `cntrl` CLI with preview/report and explicit output handling. Document the
   supported feature matrix and qualified versions. No automatic upload, firmware
   flash or source-document replacement belongs in import.

Each commit must pass focused regression checks and the repository's required
checks. Reimport the same input and compare deterministic TOML and reports.
Check no-write behavior for unsupported input and existing destinations. Compare
source-decoded meaning with compiler/exporter round trips; passing serialization
alone does not prove behavioral equivalence.

For runtime acceptance, drive representative press/hold/release/double-tap,
toggle, navigation, counter and background-engine scenarios through the existing
shared C++ emulator and compare emitted MIDI, state and ordering with stock
firmware traces. Include timing tolerances explicitly. The emulator runs cntrl,
not stock Morningstar firmware, so the reference traces must come from the stock
device or independently established evidence. Physical 6 Pro qualification does
not establish 8 or 8 Pro hardware behavior. Keep Linux/Windows host parsing,
paths and CLI checks in the normal cross-platform workflows; document any
unqualified device/firmware combination.

## Additional work beyond the prompt

None.
