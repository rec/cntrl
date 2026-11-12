# Setup backup and source synchronization

## Goal and status

An edit saved on the pedal must survive backup and be reincorporated into the
user's original host documents without losing independent host edits. Compiling
and uploading the reconciled documents must preserve the accepted result.

This is a proposed implementation contract, not authorization to implement it.
Confirm the source-ownership policy and host storage layout below before adding
automatic writes. Reuse the existing setup models, TOML loader, compiler, USB
transfer, backup, export and comparison paths. Expose the workflow through the
existing `cntrl` CLI; settle its exact arguments during implementation.

## Existing foundation

- [Backups](../cntrl/patch_switcher/backups.py) save compiled setups, editable
  exports, history and associated assets. A backup and a source synchronization
  have different completion criteria: a readable snapshot does not establish
  that it has been merged into the user's authoring files.
- The [exporter](../cntrl/patch_switcher/resolved.py) reconstructs editable values
  from the current compiled document. Derived volume and pan edits can remain
  overrides; program/bank changes or clearing an inherited setting can require
  a complete program selection. See [Patch authoring](../doc/patches.md).
- [History comparison](../cntrl/patch_switcher/history.py) compares exported
  configuration values. It does not merge changes into their original files.
- The [data-file loader](../cntrl/patch_switcher/data_files.py) composes authoring
  files. Its current merge-origin information is not a complete, retained source
  map for reconstructing those files from a downloaded setup.

## First implementation scope

Cover the setup edits currently supported by the pedal: program, bank, instrument
channel, volume, pan, brightness, screensaver timeout, MIDI-interface preference
and configurable TRS roles. Include clearing optional values. Instrument channel
edits and edits to shared compiled patch nodes must retain their actual scope;
they must not be treated as changes to just the visible occurrence.

Song/setlist synchronization, device-local expression calibration files, a
graphical editor and new on-pedal editing features are outside this first scope.
Existing backup coverage remains useful independently of source synchronization.

## Comparison baseline

Keep a host-side comparison base associated with the source project and its
logical setup names. Retain the exact compiled bytes, their canonical editable
values, target/compiler context and the source-file inputs needed to reproduce
and locate editable values. Keep source snapshots or equivalent before-images
for recovery. Use TOML for structured host records.

The base is initialized only after an upload has been verified by read-back.
A failed or partial transfer does not establish a base. Record the effective
uploaded configuration, not merely the unexpanded root file.

For each reconciliation, capture:

- **Base:** the last successfully uploaded or reconciled pedal state.
- **Pedal:** a newly downloaded saved setup.
- **Host:** the effective configuration compiled from the current authoring files.

Use the existing exporter and normalization rules for all three comparisons.
Generated font placement, checksums, file locations and serialization changes
must not masquerade as authored edits. Preserve meaningful ordering and labels.

Use existing setup names and named patch keys. Compiled offsets and list indices
are not stable identities. A renamed, deleted or reordered anonymous entry that
cannot be matched unambiguously requires review.

Without a valid base, backup and download still work. Offer a host/pedal
comparison, but do not infer which side changed or automatically incorporate it.
Retain raw bytes for an unsupported compiled version and report that source
reconciliation requires a compatible decoder; do not invent a migration.

## Proposed merge rules

| Change relative to Base | Proposed result |
| --- | --- |
| Only Pedal changed | Incorporate the pedal value. |
| Only Host changed | Keep the host value. |
| Both changed to the same value | Keep that value without a conflict. |
| Both changed the same value differently | Require an explicit resolution. |
| Edit conflicts with deletion, rename or ambiguous list movement | Require an explicit resolution. |

Compare configuration values, including the difference between an absent value
and an explicit override. Show each conflict with its base, host and pedal
values, proposed destination and affected setups. Preview and unresolved
conflicts do not change authoring files, the pedal or the comparison base.

After a successful, explicitly accepted reconciliation, advance Base to the
captured Pedal state. Host-only choices remain differences against that base.
Do not advance it to a merged host result that has not been uploaded. A later
verified upload advances Base to the newly uploaded result. Repeating an import
must be harmless, including after a subsequent host edit or intentional revert.

## Source ownership and review

1. Track the source location of an editable value through existing composition
   and compilation. Keep this provenance on the host unless an actual loss of
   information proves that compiled metadata needs extending.
2. For an unambiguous, setup-local value, propose a targeted TOML edit. Preserve
   comments, ordering, references, relative paths and unrelated formatting using
   the existing TOML tooling. A no-change import must not rewrite files or expand
   defaults throughout them.
3. For a derived patch, retain its parent and expressible overrides. When a pedal
   edit requires a full selection, show that materialization explicitly and keep
   the edited MIDI values and displayed label. Do not restore stale catalogue
   metadata merely to preserve the old spelling.
4. A change to a shared definition or instrument can affect other setups. Show
   those consumers and require a choice before modifying the shared source. If a
   setup-local override is expressible, present it for review; do not silently
   create an override file or rewrite a shared catalogue.
5. If ownership or occurrence matching remains ambiguous, retain the downloaded
   snapshot and report the unresolved change for manual placement. Matching by
   display name or by the nearest list position is insufficient.

Before implementation, confirm whether reviewed shared-source edits may be
applied automatically after approval, and where host baselines and recovery
snapshots belong. They must remain accessible with the project and must not be
treated as disposable caches. Do not add another configuration format or a
second compiler to solve source ownership.

## Backup, apply and recovery

1. Download the saved pedal state and required assets to a separate snapshot.
   Keep the exact bytes alongside the editable export. A disconnected transfer
   or missing required asset must be reported; never label a partial snapshot a
   complete backup. Observe the existing unsaved-edit and transfer locks.
2. Compile the current host inputs, compute the proposed merge and show a preview.
   The user resolves conflicts and accepts the destination edits explicitly.
3. Stage complete candidate documents and compile them before replacing originals.
   Compare their canonical exports with the agreed merged values. Recheck that
   the source files and baseline still match the reviewed inputs before applying.
4. Keep original documents and a recovery record before publication. Replace each
   file atomically where the platform supports it. Multiple file replacements
   are not one atomic filesystem operation: an interruption must leave enough
   recorded state to restore or complete the apply before another sync proceeds.
   Report incomplete application and leave Base unadvanced.
5. Publish the new comparison base only after the accepted document changes are
   complete and verified. Keep the downloaded snapshot available for recovery.
   Incorporation does not automatically upload to the pedal.

A later explicit upload must preserve every setup the user intends to retain.
The current bundle upload replaces the whole collection and bypasses individual
setup history archiving. A partial bundle must not silently discard unrelated
setups. Check every replaced setup for unreconciled pedal edits; retaining a
setup's name while overwriting its changed contents is not preservation. Resolve
those edits before replacement. Use explicit backups for this workflow;
automatic history creation for bundle replacements remains a separate issue,
not a prerequisite assumption.

## Delivery and acceptance

Start with one setup in one TOML file, then composed documents and shared
definitions. Extend the existing workflow rather than adding parallel commands
for each kind of setting. No device write is needed to preview or apply a host
merge; only a separate explicit upload changes the pedal.

- Prove every currently editable field survives pedal save, reboot, backup,
  export, source incorporation, compilation, upload and another download.
- Exercise plain selections, derived patches, named references, shared nodes,
  inlined cross-setup patches, shared instruments and layered authoring files.
  Verify actual edit scope, labels, comments, paths and unchanged consumers.
- Test pedal-only, host-only, identical and conflicting changes, clearing values,
  reordered/deleted entries and an absent or unsupported base.
- Import twice with no intervening changes. Then edit or revert a host value
  after the first import and repeat; the old pedal edit must not overwrite it.
  Preserve host-only changes across subsequent imports and uploads.
- Inject disconnects, missing assets, compile failures, source changes during
  review, failed file replacement and interruption between replacements. Keep
  recoverable originals, report incomplete work and do not advance Base early.
- Re-upload one reconciled setup in a multi-setup collection and verify unrelated
  setups, including independently edited pedal data, history, selection and
  required assets remain intact. Confirm the accepted semantic changes and
  stable export/recompile behavior.

Use focused host and native tests for pure merge, export and persistence logic.
Exercise host file handling on macOS, Linux and Windows through the existing
test workflows without dispatching a workflow for every commit. Separately
demonstrate the complete edit-to-source-to-pedal round trip on physical hardware;
automated serialization tests alone do not establish that result.

## Additional work beyond the prompt

None.
