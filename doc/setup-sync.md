# Bringing saved pedal edits back into authoring files

`cntrl setup sync` compares three versions of each setup: the last verified
upload or accepted import (Base), the current saved Pedal setup, and the current
compiled Host documents. It writes a review and recovery snapshot to a visible
`setup-sync/` directory beside the first root setup document. Keep this directory
with the project and include it in backups. It contains source before-images,
compiled setups, editable exports, required fonts/movies/MIDI files, and TOML records.

Use the same document expressions, target, definitions and tables as for copying:

```sh
cntrl setup sync Show=show.toml + shared.toml --target target.toml --port PORT
```

Preview saves the pedal state but changes neither the authoring documents nor
the pedal. `review.toml` lists each changed value, its presence or absence,
Base/Host/Pedal values, proposed source location, and known affected setups.
`pedal-choices.diff` shows source edits if all placeable pedal choices are accepted.
Unchanged defaults and unrelated formatting are left alone. Missing settings are
placed in the nearest authored table, or the first root file for a global default.

Pedal-only edits default to `choice = "pedal"`. Host-only edits stay on the host;
equal changes need no resolution. Conflicting changes, ambiguous anonymous-list
movement, and shared-source edits require an explicit choice. Edit only `choice`
in the report, setting each unresolved entry to `"host"` or `"pedal"`. Shared
edits list their consumers; include all those setups in the preview before
accepting a shared edit. Accepting it also accepts the same source-field change
for those consumers. Unknown consumers outside this project's retained records
cannot be discovered automatically.

Apply the reviewed choices explicitly:

```sh
cntrl setup sync --apply setup-sync/review-EXAMPLE/review.toml
```

Apply rechecks source and baseline checksums, compiles staged candidate files
through the existing compiler, and compares the result with the reviewed values.
Only then does it replace changed source files. Comments, ordering and relative
asset paths survive targeted TOML edits. A derived volume/pan edit stays an
override when possible; a changed program/bank or cleared inherited value can
require a complete program selection. The report identifies that materialization.
Unmapped edits require manual source placement and a fresh preview. There is no
matching by display name or nearest list position.

An accepted import advances Base to the captured **Pedal** state, including when
Host choices differ from it. It does not advance Base to an unuploaded merged
Host result. Repeating an import therefore preserves subsequent host edits and
intentional reverts. Apply never uploads. Use the existing `cntrl setup copy`
command separately to upload the reconciled documents.

Without a baseline, preview offers an explicit two-way comparison; it cannot
infer which side changed. Resolve all differences before an initial replacement.
A verified upload establishes or advances Base only after read-back and batch
completion. Uploads retain unrelated setups and reject replaced setups with
unreconciled pedal edits. They retain a snapshot of the collection before upload,
check it again before replacement, and relink the combined bundle. Existing
selection and history remain on the pedal. Bundle replacement does not create
automatic history revisions.

Multiple source-file replacements are not a single filesystem transaction.
Before publication, Apply retains every original/candidate and writes
`setup-sync/pending.toml`. If interrupted, further synchronization or uploading
stops until the originals and baseline are restored:

```sh
cntrl setup sync --recover setup-sync/pending.toml
```

Recovery refuses to discard a source edit made after the interruption. Retain
the snapshot and restore such files manually before recovering. A snapshot with
`complete.toml` has all required assets; a failed download leaves raw bytes for
diagnosis without marking the snapshot complete. Unsupported binary versions
are retained with a decode error and require a compatible compiler/decoder.

This workflow covers saved setup settings and patches. Song/setlist source
synchronization and device-local expression calibration are separate concerns.
Native receiver tests exercise import, compilation, upload, read-back and
failure recovery. Physical edit/save/reboot qualification remains a separate
hardware acceptance step.
