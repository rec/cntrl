# Already-sent stipple trial

The full pman setup was selected on the pedal before changing firmware code.
It reported `CNTRL READY 11 34583 43363d4a`, with all 256 patches and the current
solid `half` indication. The previous Caveat/Courier font demo remains stored
separately. Selecting pman required no firmware upload because the installed
firmware already supports it. No MIDI send was requested by the host.

The trial changes only the already-sent appearance. The controller marks the
affected patch-name line as stippled while retaining its resolved foreground
color. Detection of matching instrument/program/bank addresses, successful
sends and undo behavior is unchanged. The document format and saved setups
are unchanged; `half` is still encoded but is not used for this trial's
already-sent indication.

After composing the glyphs, the display renderer restores each affected line's
background at alternating checkerboard pixels. Pixels where `(x + y) & 1` is
zero retain their original foreground coverage. Applying the mask after the
whole line preserves overlapping italic strokes and keeps a common pattern
across glyphs. Category headings, other windows and padding are unaffected.

Portable controller tests cover indication state across successful/failed sends,
aliases and undo. Native renderer tests compare the full framebuffer with the
solid version, check both checkerboard phases, and verify restoring solid text
repaints the original image. Both test suites pass.

The combined stock/cntrl trial image passed the ARM build and boot-envelope
checks. It is `firmware/build/boot-switch/combined.hex`, SHA-256
`5f12d6a74e88d561d33c28a9fd06659c0c75ebc104833da9e38f219e0ccc56c6`.
RAM1 leaves 96,640 bytes for local variables; RAM2 leaves 309,216 bytes for
the heap. These are linker budgets, not runtime peak measurements.

The trial has not been flashed. The pedal remains on the solid-color pman
version for comparison.
