# Setup revision history

Status: implemented; not flashed or physically qualified. See
[usage and implementation limits](../doc/setup-revision-history.md).

## Goal

Implement the [setup revision history feature](features.md): keep named revisions
of a performance setup, compare changes, and restore an earlier configuration.

Use complete copies. Setup documents are tiny relative to card capacity, so
there is no need for deltas, compression, deduplication, a database or a small
rolling history limit. Keep revisions until the user explicitly deletes them.

## Storage

Keep the existing two-slot current-setup storage and its verified commit path.
Those slots protect an interrupted save; revision history serves a different
purpose and must not replace that protection.

Give each setup a separate history directory containing one complete record per
revision. A record contains:

- The exact compiled setup bytes, including authored metadata.
- A monotonically increasing revision number, unique within that setup's history.
- An optional user-supplied name, such as `Friday rehearsal`.
- A timestamp when a reliable clock is available.
- Length, checksum and completion information, following the existing record
  validation approach.

The revision number identifies a revision and determines ordering. Names are
labels and need not be unique. Missing clocks do not prevent saving history.
Do not depend on timestamps for uniqueness or ordering.

Keep metadata and document bytes in one record so they cannot become mismatched.
Revision records are immutable. A name is supplied when creating a named copy;
renaming existing revisions is outside the initial scope.

No separate persistent index is needed initially. Enumerate filenames to find
revision numbers; read labels only for the displayed page of results. Use bounded
buffers and incremental directory traversal, not a RAM copy of the entire history.
Allocate the next number without overwriting any existing file, including an
incomplete record left by a failed write.

## When to save a revision

Before replacing a valid current setup, save and verify a full copy of that
outgoing setup in history. This applies to uploads that overwrite a setup,
saved pedal edits and restoration of an older revision.

Also provide **Save named revision**, which snapshots the current saved setup
without changing it. Unsaved edits must be saved or discarded first.

The current setup is shown separately from its archived revisions. A newly
created setup needs no automatic history entry until it is replaced; it can
still be explicitly saved as a named revision immediately.

Do not create revisions for navigation, selection, MIDI sends, temporary edits
or cancelled changes. If replacement bytes are identical to the current bytes,
skip the automatic snapshot. Explicit named snapshots are still allowed.

Duplicate snapshots after an interrupted operation are acceptable. Avoid adding
a deduplication system just to eliminate occasional extra copies.

## Saving and interruption

1. Validate the proposed replacement using the existing document checks.
2. Write the outgoing setup to a new history record, finish it and verify it.
3. Only then perform the existing two-slot save of the replacement.

If history writing fails, leave the current setup unchanged and report the
failure. Do not silently overwrite it without keeping the promised revision.
If the current-setup save fails afterward, its existing recovery mechanism
applies and the extra history copy is harmless.

Ignore incomplete or checksum-invalid history records when listing usable
revisions. Never overwrite them while allocating a new number. History failures
must not prevent booting or playing a valid current setup.

Do not automatically prune history when space runs low. Report that saving
cannot proceed until the user frees space. History browsing and writes happen
outside timing-critical MIDI handling, using the existing bounded storage work
pattern where possible.

## Browsing, comparison and restore

Add a History action for a selected setup:

1. Browse revisions newest first, showing revision number, optional name and
   available timestamp. Include an action to save a named revision of Current.
2. Select a revision to compare it with the current saved setup.
3. Choose Restore or Back.

Comparison is read-only and sends no MIDI. Restore requires an explicit action
from the comparison view; browsing must never activate a historical setup.

Compute comparison from decoded documents on demand. Compare user-visible
configuration values, including routing, programs/banks, patch order and names,
controls, pages, styles and category assignments. Exclude record bookkeeping,
checksums and compiler-generated layout details. Keep list order significant.
Use existing export/decoding logic rather than maintaining a second interpreter
for the compiled format.

Keep the first comparison UI plain: changed configuration paths and old/new
values, with added/removed entries where appropriate. No graphical diff, patch
matching heuristics or merge tool. The host CLI can show the complete comparison;
the pedal can present a browsable compact summary. Naming revisions is available
through the host CLI, without adding a foot-operated text editor.

Restore copies the selected revision into the normal save path. It first archives
the outgoing current version, then saves the selected bytes as the new current
setup. The historical record remains unchanged. This makes a restore reversible
by restoring the version just archived. Selection and activation follow the
existing setup-save behavior; restoration itself must not send MIDI.

## Retention, deletion and assets

Retain every revision by default. Provide explicit deletion of a selected revision
and an explicit Clear history operation. Neither changes the current setup.
Ordinary setup deletion retains its history; history remains accessible by setup
name through the host CLI even when there is no current setup. Recreating the same
setup name continues that history. Avoid adding a separate identity system now.

Complete document copies include their embedded metadata, not external asset
files. Retain referenced immutable font assets so older revisions remain usable;
do not add automatic asset garbage collection. Validate asset availability before
restore and report missing assets without replacing the current setup. Icon
references remain metadata while icon rendering is deferred.

Historical bytes may belong to an older compiled format. Preserve them, but
restore only formats supported by the running firmware. Unsupported revisions
remain downloadable; explain that recompilation with suitable host tooling is
required. Do not introduce firmware migration machinery for this feature.

## Implementation sequence

1. Add revision enumeration, read, append and explicit deletion operations to
   setup storage, reusing current validation and bounded I/O patterns. Keep
   target filesystem details in the target adapter.
2. Add the outgoing-snapshot step to all current-setup replacement paths and
   implement explicit named snapshots and restore through that same path.
3. Add host commands to list, name, download, compare, restore and delete revisions.
   Implement one comparison model shared by the host presentation and pedal
   summary, without storing redundant diffs in revision files.
4. Add the pedal History action, comparison summary and explicit Restore action.
5. Document storage behavior and verify it with failure-injection tests and a
   large history. Deploy and verify pedal behavior after implementation.

## Acceptance checks

- Upload, edit-save and restore all preserve the outgoing saved bytes.
- Named snapshots preserve their labels and exact document bytes.
- Unchanged automatic saves, browsing, MIDI sends and cancelled edits add no history.
- Comparison reports semantic changes without changing selection or sending MIDI.
- Restore is reversible and does not mutate the source revision.
- Failed or interrupted history writes leave the current setup usable; interrupted
  replacement writes retain the existing recovery guarantees.
- Missing assets, unsupported formats and damaged revisions fail clearly before
  replacing the current setup.
- Thousands of revisions do not require unbounded RAM, startup scanning or a
  small retention cap. Normal performance does not scan history.
- Explicit deletion affects only the selected history records; ordinary setup
  deletion preserves recoverable history.

## Additional work beyond the prompt

None. Implementation was explicitly requested without flashing.
