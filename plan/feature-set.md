# Feature implementation plan

Add the missing MIDI controller features to cntrl as independently reviewable
items. This is a plan, not authorization to implement every item immediately.
Product-specific integrations, device-name databases and proprietary protocol
handlers are excluded. Use standard MIDI messages and configurable mappings
instead of hard-coded knowledge of particular instruments or effects.

MIDI routing and processing are implemented; see the
[usage guide](../doc/midi-processing.md).
Remaining qualification is tracked in [midi-processing.md](midi-processing.md).

## Delivery rules

- Reuse cntrl’s existing patch executor, switch assignments, output scheduler,
  setup storage and target adapters. Extend those paths rather than creating
  parallel execution or configuration systems.
- Each item includes its configuration, runtime behavior, user feedback,
  documentation and focused automated tests in one deliverable.
- Keep hardware access in target adapters. Declare supported capabilities and
  reject unsupported assignments when compiling a setup.
- Preserve responsiveness under output backpressure. Define cancellation,
  disconnect and setup-change behavior before implementing each feature.
- Automated checks establish logic and serialization correctness. Features that
  touch physical connections also require separate hardware qualification.
- Follow existing setup compilation, export and history conventions. Do not add
  compatibility formats or migrations without an explicit requirement.

Dependencies below identify prerequisites, not a requirement to implement the
entire plan. An item is complete only when its acceptance criteria are met.

## 11. MIDI CC waveform generators

**Outcome:** Start or stop an LFO that sends MIDI CC values, such as a
repeating filter sweep or automatic expression-pedal movement.

**Scope:** Generate MIDI CC values using sine, triangle, saw, square or random
shapes, with configurable range, rate and destination. Support one-shot and
repeating operation, with either an independent rate or clock divisions. Define
replacement, cancellation and final-value behavior.

**Dependencies:** The existing [MIDI Clock generator](../doc/midi-clock.md)
for clock-synchronized operation only.

**Acceptance:** Values remain within bounds; one-shot completion and stopping
are deterministic; unchanged values are not sent unnecessarily. Output pressure
cannot accumulate obsolete modulation values or delay unrelated controls.

## 12. MIDI PC/CC step sequencers

**Outcome:** A patch starts a timed sequence of MIDI Program Changes or CC
values. For example, repeat eight filter-cutoff values on eighth-note steps.

**Scope:** Keep the existing immediate `Sequence` patch semantics unchanged.
Add an explicit timed operation with step values, duration, one-shot/repeat
mode, start/stop and restart policy. Support independent timing and clock
divisions. Define how overdue steps are handled before implementation.

**Dependencies:** The existing [MIDI Clock generator](../doc/midi-clock.md)
for clock-synchronized operation only.

**Acceptance:** Steps run in order at the requested rate; stopping cancels
future steps; restart and tempo changes follow documented rules. Backpressure
remains bounded and cannot leave an uninterruptible sequence running.

## 16. Graphical configuration editor

**Outcome:** Users can edit, validate, transfer and back up setups through a
graphical interface, including without an internet connection.

**Scope:** Use the existing Python setup models, TOML configuration, compiler
and USB serial transfer protocol as the authoritative path. Provide setup
selection, patch and switch editing, port assignments, preview, import/export
and explicit save/upload. Present validation errors beside the relevant fields.
Evaluate a browser interface using Web Serial and an offline application
against actual browser and operating-system support before choosing the
delivery approach. Keep TOML import/export and the existing compiler rather
than introducing a separate configuration format or executor.

**Dependencies:** None for editing current features. Extend editing controls
alongside each subsequently completed item.

**Acceptance:** Unchanged import/export preserves supported configuration;
edits round-trip through compilation and download; disconnecting does not lose
unsaved edits or partially replace a setup. Verify the actual interface and
offline workflow separately from serialization tests.

## Suggested implementation order

Start with waveforms and timed sequences (items 11 and 12).

The graphical editor (16) can begin against the current schema without waiting
for the other items.

This order is a recommendation. Select an item and settle its stated behavior
decisions before starting implementation.

## Additional work beyond the prompt

None.
