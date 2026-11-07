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

## Remaining acceptance work

- Finish extracting page/app orchestration and remaining storage policies.
- Replace fixed control, endpoint and window records with explicit capabilities.
- Convert saved compiled setups, history records, embedded songs and animation
  references without losing pedal edits. Back up history as well as current data.
- Qualify a relocated export and an isolated portable source tree without the
  implementation or its SDK, then enforce the dependency boundary.
- Finish the porting guide with exact service contracts and build outputs.
- Build, flash and check the existing device after conversion and host checks.

This checkpoint is an intermediate extraction, not completion of the split plan.
