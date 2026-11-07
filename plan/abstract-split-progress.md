# Portable application extraction

Additional work beyond the prompt: None.

## Checkpoint

The portable package now owns font shaping and packing, animation preparation,
Intel HEX encoding, and storage report parsing. The setup compiler and transfer
code no longer import an implementation package. Target exports provide native
font resources and explicit animation geometry.

The firmware core now owns rendering, pages, font decoding and transfer, animation
playback and sessions, startup journal policy, expression calibration, recording
file selection, setup file/history naming, input routing, cooperative scheduling,
and application/menu coordination. Implementations provide display transfers,
filesystem handles, physical inputs and transports, and memory placement.

The build command invokes the selected directory's executable and consumes its
relative artifact manifest. Target resource paths resolve relative to the source
description. Memory planning and bundle validation consume an explicit capacity
and runtime estimate; portable tests use a different capacity from the device.

Validation at this checkpoint: 832 Python tests pass. The native suite passes
with address and undefined-behavior sanitizers. Formatting, selected Ruff checks,
Ty and Python-version checks pass. The real CPU image builds. A final font
baseline correction additionally has focused pixel tests for existing compiled
text and page headings. Device measurements are recorded with its baseline.

## Portable contract and format qualification

Page/app coordination, input/debounce/deadline policy, MIDI runtime scheduling,
asset commands, startup counter, storage listing, file/slot policies and history
import now live in the core. The implementation supplies electrical samples,
display transfers, filesystem/transport handles, clocks and memory placement.

The exported target declares windows, controls, input identifiers, MIDI endpoints,
scan interval, fonts, animation geometry and memory capacity/runtime allowance.
Generated capabilities drive the adapter and core. Logical routes are independent
of physical socket ordering. CLAY v2, CEND and CINP replace fixed physical records
in document format 35. Explicit limits remain documented wire/software limits.

The offline backup converter preserves compiled patch edits and converts current
setups, historical formats 20 through 34, embedded songs and animation references.
History import preserves revision numbers, labels, timestamps and reservations,
writes the alternate slot, and checks interrupted/corrupt writes. The original
backup is immutable. The actual saved data contains two setups and 15 revisions;
all 17 converted images pass the native decoder against the exported capabilities.

Validation: 885 repository Python tests pass, as does the native ASan/UBSan suite.
With the implementation tree and SDK absent, 655 portable Python tests and the
native suite pass. The relocated export compiles a setup and prepares outline
font assets. The qualification tool enforces import/include/build/generated-code
boundaries and rejects deliberately injected violations. Ruff, formatting, Ty,
Python-version checks and the real CPU image build pass. The service/build/format
contracts and validation procedure are in `doc/porting.md`.

## Device qualification

The final image was flashed through the existing uploader using the previously
approved command form. The converted backup was restored, including both setups,
all 15 history revisions, fonts, movies, songs and the saved selection. Restore
read-back checks and the independent backup verifier pass. Both setups were then
selected and loaded successfully; `simple-setup` was restored as the selection.
The pedal reports format 35 and `starts=3 starts_status=ready`.

The implementation-specific requirement audit, final image hash, memory comparison,
recovery artifacts and exact hardware results are recorded alongside the baseline.
No implementation or automated deployment work remains. Display appearance,
physical switch feel and MIDI cable/gear behavior remain outside the automated
checks and require human observation.
