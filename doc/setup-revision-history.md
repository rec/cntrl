# Setup revision history

Every replacement of a valid saved setup first archives its outgoing bytes.
Uploads, saved pedal edits and restores use the same path. Identical replacement
bytes do not add an automatic revision. Named snapshots may deliberately copy
an unchanged setup. History has no automatic pruning or small retention cap.

History is separate from the two current-save slots. A failed history write
prevents replacement; an interrupted later save still has the two-slot recovery
protection. Incomplete records are skipped. Revision numbers are never reused,
including after deletion: empty history directories reserve those numbers.

## Host commands

Use `cntrl setup history NAME ACTION --port PORT`, with these actions:

- `list`: list revisions newest first, with optional label, epoch timestamp and size.
- `save --label 'Friday rehearsal'`: snapshot the saved current setup.
- `download --revision N --output revision.bin`: download exact compiled bytes,
  including versions unsupported by the running decoder. Existing files are not overwritten.
- `compare --revision N`: show configuration paths and old/new values against Current.
- `restore --revision N`: validate and restore through the regular save path.
- `delete --revision N --yes`: explicitly delete one revision.
- `clear --yes`: explicitly delete the setup's entire history.

Labels use the existing 63-character printable setup-name rules. The host sets
the device clock before named snapshots. If the device has no reliable clock,
timestamps are zero; revision numbers still determine order.

Comparison uses editable exports, preserving list order and excluding generated
font sizes, placement records and checksums. The pedal provides a compact summary
of changed patch records, routes, settings and authored metadata instead of a
full textual diff. Raw authored metadata comparison can report a change when
only its serialization differs; the host comparison resolves actual values.

## Pedal

Choose a setup, then History. Current offers C to save an unnamed snapshot;
use the host for names. B browses older revisions, A returns to Current. Selecting
a revision opens comparison; A/B browse the summary and Restore action. C on
Restore commits it, while F returns without changing anything. Browsing and
restoring send no MIDI. Revision number, timestamp and label identify each entry.

Restoring keeps the selected historical record unchanged and archives the
outgoing Current, so another restore can reverse the operation. Normal setup
deletion retains history; access it by setup name from the host. Recreating that
name continues the same history.

## Assets and limits

A revision copies the setup document and its authored metadata, not external
assets. Existing immutable font assets remain in storage. Restore validates the
document and stages required fonts before replacing Current; missing assets or
unsupported compiled versions fail without replacement. Unsupported revisions
remain downloadable for recompilation with appropriate host tooling.

History work runs as cooperative jobs. Each main-loop step handles one directory
entry or at most 512 bytes of file I/O. Snapshot hashing, writing and read-back
verification yield between blocks, including the outgoing current-document read.
Clear history traverses the directory once, deleting one record per step. The
pedal displays Working while a history action is pending, then shows its result
or error. Host operations receive progress replies during long jobs.

No history index is held in RAM and boot does not enumerate history. The state
machine bounds application work; an individual SD library call can still have
hardware-dependent latency. Storage calls, font loading and the existing final
current-setup activation path are not hard real-time guarantees. Physical timing
qualification is still required.

Native tests cover reversible restores, failed writes, deletion, interrupted
records, more than 1,000 revisions, and per-step I/O limits with a 64 KiB document
and more than 2,000 directory entries. Python tests compare configurations and
exercise the real C++ command receiver through a pseudo-terminal, including
progress replies. The cooperative implementation has been flashed; post-boot
verification and physical storage timing qualification remain pending.
