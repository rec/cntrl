# Patch switcher implementation plan

Status: implemented and deployed, updated 2026-10-25. Host/native tests and target
image checks pass. Live results and remaining release/hardware checks are in the
[target deployment record](../cntrl/maker/morningstar/mc/firmware/patch-switcher.md).
The [multiple-setups follow-up](multiple-setups.md) adds named storage and editing.
The current compiled format is version 9, retaining original names and separate
program/show display overrides.

See the [implementation and host command](../firmware/README.md) and the target
maker directory for build instructions, measured capacities and remaining
physical verification.

Source: [issue #1](https://github.com/rec/cntrl/issues/1), read 2026-10-22.

## Goal and scope

Build a dedicated program-change sender with two independently maintained C++
codebases separated by an abstract hardware API. Portable code owns navigation,
button gestures, MIDI selection, history and screen layout. A target library
implements hardware access without owning application behavior.

Install `patch-switcher.hex` once. Subsequently compile a show document from TOML
or JSON and upload it to persistent storage through the running application,
without rebuilding or replacing firmware. Updating firmware remains a separate
operation.

This plan records the base issue #1 behavior. Numeric editing and named
performance-setup management have since been implemented; their follow-up plan
supersedes the original single-show storage and no-editor scope below. Synth
setup installation, expression support and vendor preset imports remain outside
the implemented application.

## Existing foundation

The Python package already provides hardware catalogues, Pydantic models,
tomlkit and tyro. Keep application settings out of those hardware models.

The original USB/button/fixed-text diagnostic is retained separately for hardware
investigation. The application now implements runtime font rendering, bounded
redraws, MIDI scheduling, USB data transfer and named persistent setups. Diagnostic
hold/double-press experiments are not patch-switcher gestures.

Keep all manufacturer-specific implementation, tests, data and documentation in
`cntrl/maker/morningstar/mc/`. Consult its
[firmware baseline](../cntrl/maker/morningstar/mc/firmware/README.md),
[hardware map](../cntrl/maker/morningstar/mc/mc6-pro-pin-map.md) and
[recovery evidence](../cntrl/maker/morningstar/mc/mc6-pro-recovery.md).
Record further target findings there, not in this portable plan.

## Agreed behavior and implementation measurements

The behavior decisions below are resolved. Concrete capacity limits remain an
implementation measurement task, not an open user preference.

1. **Instrument identity (resolved).** Each show-local routing alias (`vl`)
   specifies separate `instrument` and `setup` fields, for example
   `instrument = 'vl70'` and `setup = 'stock'`. Their pair identifies the
   addressing rules and instrument table; setup names need not be globally
   unique. Channel and output belong to the show-local routing entry.
2. **Program numbering and banks (resolved).** Program numbers in
   show documents, shorthand and instrument tables use 1..128. The compiler
   subtracts one to produce MIDI PC bytes 0..127. Channel numbers use 1..16,
   converted to MIDI channel fields 0..15. For a numeric program, omitting
   `bank` sends no bank-select messages and leaves the instrument's bank
   unchanged. For a program-name match, omitting `bank` uses the matched table
   entry's bank and sends the corresponding bank-select messages, if any.
   Each instrument/setup explicitly maps its logical bank numbers to MIDI
   bank-select MSB and/or LSB values. Send only the components specified by that
   mapping; do not infer them by splitting the bank number. An explicit bank
   without a mapping is a validation error.
   An explicit `bank` restricts program-name matching to that bank; it does not
   override the bank of an entry matched elsewhere.
3. **Regex matching (resolved).** Zero matches is always an error.
   Multiple matches is an error unless the show configuration's
   `take_first_match: bool = False` is explicitly true, in which case select
   only the first match. Report ambiguous matches with useful candidate names.
   Regexes search anywhere within patch names; use `^` and `$` to anchor a
   match. Match order follows entry declaration order within tables and the
   order of supplied table files. Joining metadata onto an existing entry does
   not change its position. Check every patch entry and accumulate errors
   across the complete configuration document before reporting them together
   and terminating.
4. **Table joins (resolved).** Patch identity is
   `(instrument, setup, pc[, bank])`, never name alone. Every patch has a program
   change number; bank is optional and comes last. An absent bank is distinct
   from any explicit bank, including bank 0. Tags from joined tables are combined
   without duplicates, preserving first-seen order across entries and supplied
   table files. Later tables can fill missing metadata such as names, ranges and
   descriptions. Different populated values for the same field are errors,
   included in the accumulated validation report; later tables do not override
   them. Identical populated values are accepted.
5. **Shorthand (resolved).** Field order is instrument, program,
   volume, pan, bank, with the first character as delimiter. Empty optional
   fields are allowed: `:vl:12:::0` means program 12, bank 0, with volume and pan
   omitted. Trailing optional fields may also be omitted: `:vl:12` supplies
   neither volume, pan nor bank. `-` is not an omission marker. An all-digit
   program field is a decimal PC number in 1..128; other text is a regex. Use
   `^12$` to match a patch named `12` rather than selecting PC 12. There is no
   delimiter escaping: fields cannot contain the chosen delimiter. Choose a
   different delimiter, such as `|vl|Name:.*`, or use the expanded form.
   A shorthand entry may supply a separate display-name override, for example
   `patch = ':vl:12'` with `display_name = 'My flute'`. Other separate fields may fill
   omissions in shorthand, for example `volume = 100` alongside `:vl:12`.
   Supplying a field both in shorthand and separately is an error, even if
   their values agree. Include duplicate-field errors in the accumulated report.
6. **Candidate display (resolved).** Treat `N` in the issue as `R`. Changing
   patch cycle updates R to show the new patch cycle and its remembered patch.
   Changing patch updates only R's patch within the selected patch cycle. L and C
   remain unchanged during either navigation action; navigation sends no MIDI.
7. **Undo display semantics (resolved).** Popping the only history
   entry blanks L and C, shows the undone patch in R and sends no MIDI. The instrument
   retains its last patch even though the history displays are blank. Undo
   selects the undone patch's patch cycle and cursor for R. When Undo restores a patch, C shows that patch
   with its patch cycle and effective patch label, even if another instrument
   still has a more recent remaining history entry. L shows the patch that the
   next Undo would restore, using its patch cycle and effective patch label. L is blank when the
   action stack is empty or its top action has no known previous patch. When
   undoing an instrument's first send sends no MIDI and other history remains,
   C stays unchanged. Emptying the entire history blanks C.
8. **Cross-instrument undo (resolved).** Undo reverses the latest send by
   restoring that instrument's preceding patch, leaving other instruments
   unchanged. Maintain one chronological action stack whose entries identify
   the affected instrument and its previous patch, giving per-instrument undo
   history without requiring an instrument-selection action. If that instrument
   has no preceding patch, pop the action but send nothing; its earlier state
   is unknown. For sends A:Flute, B:Strings, A:Sax, the first Undo restores
   A:Flute; the second removes B:Strings without sending anything. Undo does not
   push another action or send all-notes-off.
9. **Capacity and text (policy resolved; limits deferred to measurement).** When history is full, adding a
   new action discards the oldest action, so sending patches can continue
   (resolved). Set maximum document size, patch cycle/patch counts, text lengths
   and history depth after measuring storage and RAM use in the implementation;
   do not choose arbitrary limits in advance. Record the measured limits and
   the implemented font's supported characters in the target capabilities
   before release. Text that must be displayed but contains characters the target
   font cannot render is a compilation error (resolved). Identify the offending
   characters and source fields in the accumulated error report rather than
   silently replacing them. Undisplayed descriptive metadata is not subject to
   the display font's character restrictions.
10. **Optional controls and SysEx (resolved).** Volume uses 0..127,
    sent as MIDI channel volume (CC7); omitting volume sends no CC7 message
    (resolved). Pan uses -64..63 with 0 centered, sent as CC10 after adding 64;
    omitting pan sends no CC10 message (resolved). Message order is bank select
    (MSB then LSB when both are specified), program change, volume, then pan,
    omitting unspecified messages. Setup SysEx files are never transmitted
    during live operation (resolved). They remain optional host-side references;
    the pedal does not send them on boot, selection, Undo or document upload.
    Setup installation is outside the first release; instruments must already
    have the selected setup loaded.

## Source documents and compilation

The application package is `cntrl/patch_switcher/`,
with typed Pydantic models, compilation, binary encoding and a tyro CLI.
Use existing dependencies. TOML and JSON describe the same schema; TOML uses
tomlkit. Keep patch cycle and patch declaration order through compilation.

Separate three inputs:

- Instrument/setup definitions: identity and bank/program addressing rules,
  optionally referring to host-side setup SysEx files that are not included in
  the runtime document or transmitted by the pedal.
- Instrument tables: patch address, immutable instrument patch name, optional
  program-level `display_name`, range and
  description, and zero or more tags. A repeated table flag joins base tables
  with personal metadata using the agreed identity and conflict rules.
- Show document: routing aliases, setup references, channels and output IDs;
  ordered patch cycles containing expanded or shorthand patch entries.

The linked [VL70 table](https://github.com/rec/midi/blob/main/vl70/all-vl70.json)
is a source example, not the schema: it nests setups, banks and numbered entries
whose values contain names and sometimes range/description. Some entries lack
metadata and its numbering is human-facing. Do not copy its positional arrays
or assume its bank keys are MIDI bank bytes. A converter for that legacy format
is not required by this issue.

Compilation pipeline:

1. Parse and validate documents and table identities; normalize shorthand.
2. Resolve aliases, setups, numeric addresses and regex matches.
3. Validate MIDI ranges, output availability and target capacity. Reject empty
   shows/patch cycles and unresolved or conflicting entries with source context.
4. Resolve each patch to its display name, patch cycle, output and ordered MIDI
   message bytes. Prefer the show patch's `display_name`, then the program's
   `display_name`, then the original synthesizer `name`. Lookup always uses
   the original name. If none is available, display `PC 12`, or `Bank 3 / PC 12` when a bank is specified, using
   the configured bank number and human-facing PC numbering 1..128.
5. Encode a deterministic binary document. Include a format version, total
   length, counts, offsets and integrity checksum with explicit byte order.
   Retain original/program/show names and instrument/bank metadata for lossless
   resolved downloads under `definitions.programs` and `definitions.setups`.
   Regex selectors and unrelated descriptive table metadata stay on the host. Do not serialize C++ structs or pointer layouts.
6. Report counts, byte size and resolved routes before an explicit upload.

Validation must not stop at the first invalid patch. Check all patch entries,
collecting schema, shorthand, reference, regex and MIDI-value errors wherever
the document can be parsed. Report the accumulated errors together, identifying
each patch cycle and patch entry, then terminate with a nonzero exit status. Avoid
dependent checks whose inputs are invalid, but continue checking other fields
and entries. Do not produce or upload a binary when any errors remain.

The device independently validates lengths, offsets, checksums, supported format,
routes and capacities before activation. Shared golden binary fixtures must be
read by both Python compiler tests and the C++ decoder tests.

## Portable firmware and target API

Implemented layout:

- `firmware/include/cntrl/`: the hardware contract, decoder, controller,
  gesture handler, MIDI bridge/scheduling, named setup storage and editor logic.
- `firmware/test/`: host-side C++ tests with a fake implementation of that API.
- `cntrl/patch_switcher/` and `test/`: generic host tools and their unit tests.
- Existing maker directory: target library, build target, hardware-specific
  tests, storage allocation and deployment instructions.

The API supplies timestamped debounced press/release events, a monotonic clock,
display dimensions and text/drawing operations, identified MIDI outputs,
ordered message transmission, USB document transport and transactional document
storage. Define only the operations this application needs. Keep GPIO numbers,
peripheral types, USB stack details and storage addresses out of portable code.

The portable application maps six logical actions to controls and lays out three
logical panes using reported display capabilities. It must not assume a target
name, display controller or hard-coded panel geometry. The first target binds
those actions to A..F and panes to physical displays. Verify physical pane order.
Other layouts are not implemented speculatively; unsupported capabilities must
be reported explicitly.

Keep output names opaque and validated against the target's available routes.
Use “configurable ports” in the API, not a manufacturer-specific name. Resolve
any example alias such as `omni-1` at the target boundary. Do not infer that a
catalogued connector already has an implemented MIDI driver.

Use a cooperative loop with bounded scanning, rendering, transport and MIDI
work. Long redraws or uploads must not block button handling. The target owns
shared-bus arbitration; portable code owns redraw requests and layout. Reuse
verified low-level routines, replacing demonstration behavior rather than
maintaining a second patch-switcher implementation inside the diagnostic sketch.

## Runtime state and actions

Maintain the active document, selected patch cycle, one cursor per patch cycle and a
bounded chronological action stack. Each send records the instrument, selected
patch and its previous patch, if known. Track the latest remaining selection per
instrument. Runtime cursors and history live in RAM, not in persistent storage.

| Action | Behavior |
| --- | --- |
| Startup | Empty history; L/C blank; R first patch of first patch cycle; no MIDI |
| D / E | Previous/next patch cycle, wrapping; restore its remembered cursor |
| A / B | Previous/next patch in current patch cycle, wrapping |
| C | Send candidate; push it to history; update L/C; leave R unchanged |
| F | Empty history: no-op; otherwise pop latest action, restore that instrument's preceding patch if known, and select the undone patch in R |

Navigation never sends MIDI. Undo pops the latest action and sends its recorded
previous patch to the same instrument, if one is known, without pushing an entry.
It never substitutes a different instrument's patch from the chronological
stack. An empty stack does nothing; undoing an instrument's first send sends
nothing. Pressing C on an already-sent, greyed-out patch does nothing and never
pushes a duplicate entry. After a restoration,
C shows the restored patch's patch cycle and effective label, without an instrument
name or title. After every send or Undo,
derive L from the previous patch recorded by the top remaining action, or blank
it if no restoration target exists. If Undo sends nothing, C stays unchanged
while history remains; an empty history blanks C. R shows the undone patch even
if browsing had moved to a different patch cycle or patch before Undo.

Keep each patch's bank-select, PC and optional control messages in order on its
route. A greyed-out candidate is ignored before sending; distinct patch sends
retain their complete message sequences. Define successful sending as acceptance by the local output queue,
not acknowledgment from an instrument. Reserve space for the complete sequence
before committing history; on local failure keep history unchanged and show an
error. Undo similarly commits its history change only when any required resend
can be accepted.

## Button timing

The target supplies electrically debounced edges. Portable code implements the
separate chord window, configurable with an initial value of 100 ms.

- Delay singles from A, B, D and E for the chord window.
- If a pending button is released before its window expires, fire its single
  action immediately on release. It can no longer form a chord after release.
- Recognize AB, AD, BE and DE in either order. Consume both singles; AD opens
  setup management, while AB, BE and DE have no action. A wake gesture cannot
  also open management: release it and press AD again. Releasing a consumed
  chord must not emit singles.
- Unrecognized pairs, such as A+E, also do nothing. If the second press arrives
  while the first is still pending and held, suppress both single actions;
  their later releases do not emit singles.
- Any detected pair, recognized or not, blocks further A/B/D/E actions until
  all four buttons are released. Additional presses during that interval are
  ignored, not queued. C and F remain immediate throughout.
- C and F act on press without the chord delay, including while another button
  is pending. No hold, repeat or double-press action is required.
- Use timestamp arithmetic that tolerates clock wraparound.

The deadline is inclusive: a second press at or before 100 ms qualifies while
the first remains pending and held. Process an edge timestamped at the deadline
before expiring the pending single action. A later press cannot cancel a single
action that has already fired. Test the exact boundary, three-button overlap
and pair lockout until all A/B/D/E buttons are released, not just the four
nominal recognized pairs.

## Persistent document upload

Use a host command and a receiver in the running firmware, initially over the
existing USB serial transport. Firmware upload tooling remains separate.
Define framed begin/data/finish/status messages with declared size and checksum;
handle partial reads without treating USB read boundaries as message boundaries.

Before any storage write, establish a safe, bounded storage region in the target
library. Do not assume unused flash or a card slot is safe storage. Protect the
bootloader, executable image and unrelated saved settings. Document storage
capacity and behavior during interrupted writes in the maker directory.

Stage the complete new document while retaining the old committed one. Verify
it before committing a recoverable activation record, using two slots or
an equivalent transactional mechanism supported by the chosen storage. Never
erase the only valid document first. A checksum is integrity checking, not
authentication.

Activation behavior (resolved): after successful durable commit, immediately
activate the new show, discard old cursors and history, select the first patch
of the first patch cycle, blank L/C and send no MIDI. Retain the
old active document on validation or transfer failure. At boot, load the latest valid generation of the persistently selected named
setup, choosing alphabetically only when no selection has ever been stored.
If none exists, display an explicit no-document/error
state and accept uploads without sending MIDI. Acknowledge success only after
durable commit and verify the document identity again after reboot.

## Implementation sequence and acceptance gates

1. **Resolve semantics and write fixtures.** Settle the decisions above and
   define a small two-instrument, multi-patch cycle example in TOML and JSON,
   including a supplementary tags table and shorthand. Agree on expected MIDI
   bytes, screens and history for a short action sequence.
2. **Build the portable core.** Define the API, fake hardware and host C++ test
   build. Implement navigation, history, chord timing and layout without target
   headers. Verify startup silence, wraparound, cursor memory, all undo cases,
   including interleaved instruments and an instrument's first send, duplicate
   sends, history capacity and rendering updates.
3. **Build the compiler and decoder.** Implement models, table joining,
   resolution and deterministic encoding. Test equivalent TOML/JSON and
   expanded/shorthand inputs, missing metadata, invalid regexes, ambiguous
   matches with `take_first_match` both false and true, zero matches under both
   settings, conflicts, bounds and truncated/corrupt binary input. Verify that
   several invalid patches produce one complete error report and no binary or
   upload. Cross-check Python and C++ against the same fixtures.
4. **Integrate the target.** Add the release build target and hardware adapter.
   Implement general text rendering and bounded runtime redraws, then required
   MIDI output routes. Fit names during compilation or report errors instead of
   silently truncating them. Verify buttons remain
   responsive during drawing, and captured MIDI bytes match fixture sequences
   on every advertised output. Do not claim unsupported routes work.
5. **Implement persistence and transport.** Qualify the storage region before
   enabling writes. Test upload interruption, malformed frames, capacity limits,
   incomplete commits and reboot selection, first with fake storage and then on
   hardware. Prove previous documents survive failed replacement.
6. **Release qualification.** Produce and inspect `patch-switcher.hex` using
   established target safety checks. After a separately authorized upload,
   upload two different show documents without reflashing. Power-cycle and
   verify persistence, startup silence and the complete navigation/send/undo
   sequence. Confirm recovery still works and unrelated saved data is intact.

Each implementation commit must pass the applicable Python checks and portable
C++ tests. Build inspection is not electrical verification. Hardware testing,
flashing and storage writes are explicit later steps, not actions authorized by
this planning request. Add target-specific qualification records under its maker
directory and generic usage/format documentation alongside the new application.

## Additional work beyond the prompt

None.

## possible expansions after completion of the project

These are follow-up ideas, outside the current project's completion criteria.

### USB typing keyboard

Accept a USB typing keyboard through a target's USB host socket. Translate key
events using a keyboard layout, allowing text entry for patch searches and
keyboard navigation with arrows and Enter. Expose input through the portable
API; keep USB host initialization and keyboard drivers in the target library.
Verify socket power and keyboard operation on hardware before claiming support.

### Browser connection over USB

Provide a browser interface using Web Serial and the firmware's USB device
connection. After the user selects the pedal, exchange commands and state in
both directions: upload show documents, browse patches, trigger actions and
receive button events. Reuse the document transport where applicable. Serve
the app over HTTPS and detect browser support. The pedal continues operating
independently when the browser disconnects.

### Temporary display popups from SysEx

Receive custom SysEx commands for temporary text and simple graphics, such as
lines, rectangles and bars. Commands specify the logical display, content and
duration, with an explicit dismissal command. Render popups as overlays without
changing patches, navigation, history or saved settings. On expiry or dismissal,
redraw the current underlying interface, including changes made during display.

Add USB MIDI reception to support browser senders through Web MIDI with SysEx
permission, or other MIDI software. The same display commands can also travel
over Web Serial. Keep command decoding separate from portable rendering and
target display drivers, with bounded payload sizes and drawing work so input
handling remains responsive. This custom popup protocol is separate from setup
SysEx files, which remain excluded from live transmission.

## Target-specific capabilities

Keep pin assignments, electrical modes and physical qualification under the
maker directory. The portable application uses output IDs and reported
capabilities, not a particular jack, conductor arrangement or peripheral.
See the [target adapter and qualification notes](../cntrl/maker/morningstar/mc/firmware/patch-switcher.md#adapter-and-storage-boundaries).
