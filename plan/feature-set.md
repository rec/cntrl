# Feature implementation plan

Add the missing MIDI controller features to cntrl as independently reviewable
items. This is a plan, not authorization to implement every item immediately.
Product-specific integrations, device-name databases and proprietary protocol
handlers are excluded. Use standard MIDI messages and configurable mappings
instead of hard-coded knowledge of particular instruments or effects.

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

## 3. External auxiliary switches

**Outcome:** External switches can invoke the same assignments as built-in
ones.

**Scope:** Decode supported TRS tip, ring and combined contact arrangements,
debounce inputs and give each switch a stable identifier. Reuse press,
momentary and latched assignments. Document wiring and reject configurations
exceeding the target's capabilities.

**Dependencies:** The TRS Ports utility page provides socket role selection.

**Acceptance:** One physical action produces one intended transition; held
inputs are rearmed correctly on entry; release and disconnection have explicit
cleanup behavior. Verify simultaneous inputs and contact bounce on hardware.

## 4. Relay outputs

**Outcome:** Patches can switch amplifier channels or send analog tap-tempo
pulses through relay contacts.

**Scope:** Add explicit engage, disengage, toggle and timed-pulse operations,
including independently supported contacts and configured resting states. Keep
pulse timing nonblocking. Specify power-up, setup-exit and cancellation
behavior.

**Dependencies:** None; requires a target with supported relay hardware.

**Acceptance:** Contact states and pulse durations match configuration; a
blocked MIDI output cannot prevent a pulse from ending. Verify electrical
behavior and document connection limits for each target.

## 5. USB host and hub support

**Outcome:** Connect multiple USB MIDI instruments or controllers through a USB
hub and address each independently. The current USB host implementation handles
a single MIDI device.

**Scope:** Extend the USB host driver with hub support and independent MIDI
endpoints. Bind setup routes using available device identity and port
information. Decide how users resolve indistinguishable devices rather than
relying silently on discovery order. Show unavailable destinations without
blocking the controller.

**Dependencies:** None.

**Acceptance:** Reordering connections does not silently redirect assignments;
disconnecting one device leaves others usable; reconnection does not replay
stale performance traffic. Qualify supported hubs, power arrangements and
device counts.

## 6. MIDI routing matrix

**Outcome:** Route MIDI between DIN, TRS, USB device and USB host ports. For
example, forward DIN input to both TRS output and a connected computer.

**Scope:** Extend the existing USB MIDI Interface bridge to configured input
and output pairs, including physical-to-physical and USB-host routes. Add route
enable controls and make ownership with active diagnostic tools explicit.
Prevent internal recirculation; document that external cable loops require
deliberate routing.

**Dependencies:** None. Item 5 is needed only for multiple hosted devices.

**Acceptance:** Only selected routes forward traffic; SysEx and real-time
messages preserve their required ordering; congestion on one route does not
stall unrelated routes or the interface. Disabled routes retain no traffic for
later replay.

## 7. MIDI filtering and conversion

**Outcome:** Filter or transform MIDI on each route. For example, remap channel
1 to channel 3, convert CC 7 into CC 11, or map a Program Change to a
configured Control Change.

**Scope:** Match input port, MIDI message type, channel and message fields.
Support channel remapping, controller-number remapping, value scaling and
explicit conversion between supported message types. Define rule order and
whether a match replaces, consumes or passes the original message. Keep
converted output out of its own input path.

**Dependencies:** Item 6.

**Acceptance:** Unmatched traffic follows the stated policy; transformed values
remain valid; transformed Note On messages have matching Note Off messages.
Test multiple rules, real-time interleaving and bounded processing under load.

## 9. Shifted assignment actions

**Outcome:** Assignments can use a temporary shifted state to select alternate
actions.

**Scope:** Add shift/unshift operations and define alternate actions,
repeat-selection behavior, persistence across navigation and cleanup before
implementing the schema. Shared MIDI parameter state and selection-based enter
and exit hooks are implemented in [switch controls](../doc/momentary-latched.md).
Parameter identities synchronize assignment state automatically.

**Dependencies:** None. Programmable switch gestures support gesture-triggered shifts.

**Acceptance:** Shifted actions and restoration follow the stated lifecycle;
indirect transitions cannot recurse indefinitely; display state follows local
execution and does not imply acknowledgement by an external instrument.

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

## 13. MIDI PC/CC scroll counters

**Outcome:** Use switches to step through MIDI program numbers or adjust a CC
value, such as raising volume by five units per press.

**Scope:** Add named counters with initial value, limits, step size and
optional wraparound. Bind counters to MIDI PC or CC messages and display their
current values. Define setup-change reset behavior and whether an unsuccessful
send advances the counter. Reuse the same value for display and transmission.

**Dependencies:** None.

**Acceptance:** Limits, wraparound and negative steps behave consistently;
multiple assignments referencing a counter share one value; failed or blocked
sends follow the chosen advancement policy without display drift.

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

Start with counters (item 13), which extend existing execution paths without
new physical inputs.
Next implement waveforms and timed sequences (items 11 and 12).
Routing and conversion (6 and 7) form a separate increment. Shifted assignment
actions (9) can follow once their transition rules are agreed.

Hardware-dependent work (items 3 through 5) should follow available test
hardware and actual connection needs. The graphical editor (16) can begin
against the current schema without waiting for the other items.

This order is a recommendation. Select an item and settle its stated behavior
decisions before starting implementation.

## Additional work beyond the prompt

None.
