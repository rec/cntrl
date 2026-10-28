# Code audit: exceptional paths and resource use

Reviewed 2026-10-28 at `eab498633baa2e7556a7f0ff2cadc83209a7ea94`,
after fetching `origin/main`; the checkout was current. Findings are a backlog,
not implemented fixes. P1 means a crash or significant interruption of operation;
P2 means a correctness or reliability problem; P3 means a lower-priority
resource, naming or documentation improvement.

Layout code and layout defects are excluded, following the final instruction.
SD capacity and wasted SD space are excluded. Storage latency, failed reads and
open handles remain relevant because they affect CPU work and resource lifetime.

Hardware-specific findings are in the linked
[Morningstar appendix](../cntrl/maker/morningstar/mc/code-audit-issues.md), as
required by the repository's hardware documentation rule. It forms part of this
audit and contains issues M1–M5.

## Confirmed memory-safety failures

### C1. P1: song rename dereferences a failed lookup

**Resolved:** the rename action reports `Song read failed` and remains available
for retry. A native sanitizer regression covers failure with the upcoming song
cached, recovery and successful renaming without MIDI output.

**Evidence:** `firmware/include/cntrl/songs_ui.h:53`,
`firmware/include/cntrl/songs.h:63` (`Songs::song`).

The Rename action passes `songs_.song(song_id_)->name` directly to `begin_name`.
`song()` returns null on a failed storage read. Its single-entry cache can hold
the upcoming song rather than the song being renamed, so a previously successful
selection does not protect this call.

**Reproduced:** create songs A and B, make B the next setlist item, open A's
actions, then fail storage reads and select Rename. UBSan reports member access
through a null pointer at line 53; ASan reports a read fault at address `0x4`.
The probe used the existing native fake storage and board, without device access.

**Proposed correction:** check the lookup and report a read failure while keeping
the action recoverable. Add a fault-injection regression with another song cached.
This is a null dereference, not a use-after-free.

### C2. P2: accepted malformed MIDI batches read past their storage

**Evidence:** `firmware/include/cntrl/midi_bridge.h:110` (`pedal`) and
`:141` (`service`), especially lines 153–159.

`pedal()` accepts any nonempty sequence up to 14 bytes. `service()` assumes a
complete sequence of two- or three-byte channel messages, reads the next message
without checking remaining length, and only finishes on exact offset equality.
For example, `{0xc0}` is accepted as a one-byte batch. The offset skips past its
length, never completes, and eventually reads beyond `Batch::bytes[14]`.

**Reproduced:** enqueue that one-byte batch and service it eight times against an
accepting sink. UBSan reports out-of-bounds indices 14, 15 and 16; the bridge
still reports a pending batch. Current controller/song encoders produce complete
batches, so this is a latent API fault, not a demonstrated normal-user trigger.

**Proposed correction:** reject incomplete/unsupported channel-message batches
before enqueueing, preserving the all-or-none contract. Test truncated two- and
three-byte messages and invalid statuses.

## Failure handling and scheduling

### C3. P1: song operations have bounded buffers but unbounded execution time

**Evidence:** `firmware/include/cntrl/songs.h`, `commit()` at line 358,
`validate_import()` at line 292, `crc_range()` at line 258;
`firmware/include/cntrl/songs_transfer.h:36`.

Each edit synchronously scans the complete library to compute a checksum,
streams it again to write, then reads/validates the committed record before
returning. Import validation performs a complete identity scan for each identity
and reference, making it quadratic in collection size. A 256-byte local buffer
limits memory, not work per main-loop iteration. There is no collection-count
ceiling and no yielding within these operations.

Large imports or live edits can therefore delay MIDI servicing, button releases
and host replies for the entire operation. This conflicts with the cooperative,
bounded execution requirement in `doc/feature-modules.md` and the live-editing
intent of `plan/songs-and-setlists.md`. Actual hardware delay is not measured here.

**Proposed correction:** use incremental jobs with a bounded work step and a
completion notification, following `Revisions`. Test aggregate reads/writes per
step, not just individual I/O sizes. Do not put storage or validation inside an
interrupt handler.

### C4. P2: setup browsing repeatedly validates the whole catalogue synchronously

**Evidence:** `firmware/include/cntrl/setup_ui.h:114` (`browse`),
`firmware/include/cntrl/documents.h:39` (`next_valid`), `:57` (`rotate`),
`:350` (`load`).

One browse/rotate action visits every valid setup; each visit reads and validates
both slots and rereads the selected slot. The caller cannot yield between setups.
Even a button press that moves one catalogue entry can perform many full-document
reads/checksums. This predates the cooperative history implementation and remains
outside its per-step bounds.

**Proposed correction:** advance catalogue traversal/validation over bounded work
steps and notify the requesting action on completion. Measure scheduling latency
as catalogue size grows. This is an execution issue, not a layout issue.

### C5. P2: failed edit saves discard the editable state too early

**Evidence:** `firmware/include/cntrl/documents.h:116` (`edit_save`),
`:145` (write failure), `:218` (`history_complete`);
`firmware/include/cntrl/setup_ui.h:27` and `:37`.

`edit_save()` clears `dirty_` as soon as the asynchronous archive job is accepted,
before history and current-save writes succeed. On a later failure, the UI enters
Failed; acknowledging that screen closes it and redraws the old active document.
Reentering editing copies the old active bytes over staging. The user cannot retry
the unsaved edits, although the old saved document remains protected.

**Proposed correction:** retain an explicit pending-edit ownership/state until
verified success, and provide retry or discard on failure. Test a history failure
and a current-slot write failure after `edit_save()` has returned successfully.

### C6. P2: abandoned song imports have no storage-abort path

**Evidence:** `firmware/include/cntrl/songs_transfer.h:9`, `:13`, `:35`;
`firmware/include/cntrl/songs.h:223`–`:226`.

Timeout and malformed DATA clear `receiving_` without finishing or aborting the
storage writer opened by `import_begin()`. END with an incomplete payload also
short-circuits before `import_finish()`. The transfer consequently reports idle
while an incomplete write session still owns resources. There is no cancellation
method on `Songs` to release that ownership.

**Proposed correction:** provide one abort operation and use it on timeout,
parse/write failure, incomplete END and replacement BEGIN. Abort must never commit
the partial import. Test that every accepted begin eventually finishes or aborts.
This is retained resource ownership, not evidence of an accumulating heap leak.

## RAM and confusing names

### C7. P3: four full-document buffers reserve 256 KiB, with obscured ownership

**Evidence:** `firmware/include/cntrl/documents.h:180`–`:189`,
`:255`–`:263`; `firmware/include/cntrl/hardware.h:7`.

`active_`, `staging_`, `history_data_` and `delete_names_` each reserve 65,536 bytes
for the lifetime of Documents. `delete_names_` is also the complete current
document used by history and comparison, despite its name/comment describing
deletion names. The history read copies a valid candidate from `history_data_`
into this fourth buffer while examining the two current slots.

These buffers have real uses, so removing one without changing the read sequence
would be wrong. Nevertheless, a two-slot selection/read strategy could avoid a
full extra candidate copy and potentially recover 64 KiB. This is a resource
opportunity to prove, not a claim that all four buffers are unnecessary or leaking.

**Proposed correction:** first document/rename the scratch buffer by its actual
roles; then evaluate selecting/verifying a slot before retaining its payload.
Preserve active playback bytes, incoming replacement bytes and interruption safety.
Use a memory-budget assertion to make any recovered RAM measurable.

### C8. P3: history comparison emits inconsistent configuration paths

**Evidence:** `cntrl/patch_switcher/history.py:49`–`:75` (`visit`).

Changed existing scalar values use `path.lstrip('.')`, whereas added/removed
dictionary keys and list entries retain the initial dot. Thus one comparison
uses both `styles...` and `.styles...` depending on the kind of change. These are
presented as configuration paths, but equivalent locations have different names.

**Proposed correction:** construct paths consistently for every change type.
Verify addition, removal and value replacement at root and nested locations.

## Specification and documentation drift

### C9. P3: implementation-status statements are stale

**Evidence:** the opening status paragraphs of `plan/songs-and-setlists.md`,
`plan/formatting.md`, `plan/midi-interface.md` and `doc/feature-modules.md`.

The songs plan still presents the implementation as on its feature branch, though
it is in the audited main checkout. The module guide's status mentions only the
initial cable tester, while the repository includes interface, songs and Panic.
Formatting/interface plans retain pre-deployment wording; deployment evidence is
recorded separately in the hardware documentation linked from the appendix.
These statements make it hard to distinguish implemented, installed and physically
qualified work. A deployment does not establish physical qualification.

**Proposed correction:** update status summaries against commit/deployment records
and retain explicit physical-verification gaps. Do not mark deliberately deferred
features as bugs: icon drawing and advanced song sections are explicitly deferred.

## Callback/interrupt opportunities

- C3 and C4 are good candidates for cooperative job completion callbacks or the
  existing polled completion pattern. The important change is yielding work, not
  adding callbacks around an unchanged blocking function.
- M3 identifies a useful output-ready/completion boundary for serial replies.
- M1 needs an ownership handoff completion condition before another producer runs.
- No new hardware interrupt is justified by this source audit. Interrupting a
  blocking operation does not make storage safe to call from the ISR. Keep device
  interrupts short and defer work; first measure missed service deadlines.

## Memory/lifetime review and limits

The two sanitizer probes above establish an out-of-bounds read and a null
dereference. No confirmed heap leak or use-after-free was found. Generic document
views borrow their byte buffers; the inspected reference resolver consumes its
view synchronously. Song/setlist lookups borrow mutable single-entry caches, so
retaining their pointers across other lookups is unsafe even though no allocation
is freed. Existing callers frequently copy these values; C1 is the demonstrated
failure path. Font-cache ownership and peak allocations are discussed in M4/M5.

This was a targeted source audit plus two isolated native fault probes, not an
exhaustive proof of safety. The temporary probe reused the existing song-test
fakes and was compiled with `-fsanitize=address,undefined`; no production source
was changed. No full application, hardware test, firmware build or flash was run.
Review covered the controller, documents/history, songs/transfers, MIDI bridge,
host transfer/history/song code and relevant hardware ownership adapters.

## Additional work beyond the prompt

None. The hardware appendix is part of the requested report, separated only to
comply with the repository's hardware-specific documentation location rule.
