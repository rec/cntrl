# Persistent-format recovery qualification

This is the MC adapter procedure for qualifying the generic binary-format plan.
The implementation is development format d45. No public f0 tag is authorized by
this document. Flashing and physical interruption tests remain deferred until
the user explicitly resumes hardware qualification.

## Retained development inputs

Header inspection of the tracked archive confirmed these older device captures:

| Archive path below `archive/2026-11-09-tmp/backups/` | Core format |
| --- | --- |
| `cntrl-trs-ports-device-backup/simple-setup.bin` | 28 |
| `cntrl-before-latest-88mcxda6/simple-setup.bin` | 29 |
| `cntrl-before-latest-88mcxda6/format-30/simple-setup.bin` | 30 |

These are archived recovery material, not supported automatic d45 import inputs.
Their original bytes must remain unchanged. Do not rebuild them with current
authoring defaults and describe that as a lossless binary conversion. The archive
also retains firmware images and its manifest. Recovery of these versions needs
their matching historical host/firmware, assets and data.

The supported automatic development setup inputs are 36, 38, 39, 40, 41, 42,
43, 44 and 45, plus d44 envelopes. Bare `CSTB` bundles and `CSG5` song journals
are legacy inputs. New bundle/library envelopes and integrity-protected `CSG6` journals are
d45. The generic specification and fixtures are authoritative for these bytes.

## Before upgrading a device

1. Stop editing and recording. Retain the currently tested host revision and its
   complete firmware build output, including artifact manifest and hashes. A
   newly built image is not evidence that it matches the currently running one.
2. Use `cntrl setup backup` with a new destination. Verify its manifest and keep
   the raw active journals, assets, history reservations, recordings and counter.
   Retain a card image as the physical recovery reference during qualification.
3. Use `cntrl setup convert` to a separate destination for the intended target
   profile. Missing assets, unsupported formats, opaque reference remaps or
   incomplete captures must stop this step. Keep the original directory.
4. Build the new firmware with `cntrl build` and retain its complete output.
   Do not flash until hardware qualification is authorized.

## Authorized hardware qualification

Once explicitly authorized, use the existing `cntrl flash --skip-build` command
with the retained build output, target directory and device port. Data restoration
is a separate `cntrl setup restore` operation. Do not treat an interrupted flash
as an interrupted data stage.
If the new firmware cannot read the old setup core, restore can install the
verified offline conversion from recovery mode. It first captures the current
raw stores with digests and retains their original device root. Unknown active
documents are not merged or reported as converted. An ordinary unconverted
backup is refused for this recovery-install path.

The adapter maps coordinated mutable stores into generation directories. SD
`sync()` and read-back are required before publishing the alternate activation
slot. Ordinary saves use their existing journals. A valid startup collection is
marked as requiring d45 before runtime saves are enabled, preventing older d44
firmware from silently choosing a stale song generation. This is a software
ordering guarantee; actual SD controller behavior under power loss still needs
the following tests.

Qualify interruptions during transfer, inactive journal writes, completion-marker
writes, each activation-record boundary and flashing. After each reset, verify
which complete root is selected. Incomplete or incompatible collections must
provide raw USB recovery access without musical execution. Check USB reconnect
and that MIDI arriving during staging is discarded rather than replayed.

After a successful upgrade, use `cntrl setup verify` and exercise setup selection,
numeric edit/save/reload, history restore, song captures, setlist repetition,
undo across reboot, assets, source synchronization and MIDI timing/order. Measure
startup/load time, save latency and peak memory on the pedal. Do not infer these
measurements from the host emulator or linked image alone.

## Recovery and rollback

An interrupted data stage leaves the last committed root selectable. Use
`cntrl setup recover --root ROOT` only for a retained completed root supported
by the running firmware. Recovery validates the bundle, library and selected
setup before committing it. Back up newer edits first.

For a damaged flash, recover with the retained matching image through the existing
bootloader/upload-helper procedure. If the historical firmware does not support
the current activation format, restore the original four mutable stores and
activation journals exactly from the raw backup/card image with the card reader,
then boot the matching image. Use its matching historical host when needed.
This is restoration of an original firmware/data pair, not a downgrade converter.

Preserve recordings made since the original backup and the maximum valid startup
counter before restoring older mutable stores. Never overwrite a recording with
different bytes under the same filename or copy an old counter over surviving
new recordings. Adapter calibration and uncertain execution continuations must
not be replayed onto another unit or automatically resumed.

Only after these physical checks pass should the public f0 specification and
fixture snapshot be frozen and tagged. Released fixtures must remain immutable;
the development fixture regeneration command must not overwrite them.
