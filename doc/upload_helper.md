# Firmware upload helper

Firmware upload helpers belong to the selected implementation and are accessed
through `cntrl upload-helper --target-dir TARGET_DIR`. The selected directory
provides `upload_helper_entry.py`; the common CLI does not import maker code.

The independent operations are:

```bash
cntrl upload-helper --target-dir TARGET_DIR serve
cntrl upload-helper --target-dir TARGET_DIR bootloader
cntrl upload-helper --target-dir TARGET_DIR upload --image /absolute/path/to/firmware.hex --sha256 DIGEST
```

`serve` starts the helper in your Terminal. `bootloader` requests bootloader
entry without uploading. `upload` sends an existing image without rebuilding or
transferring setups. The target owns validation and device access; uploader
output and exit status are returned unchanged.

Use the running, user-launched upload helper for firmware flashing. Reuse that
process; do not start a second server. Each upload request supplies an absolute
HEX image path inside a registered Git worktree and its exact SHA-256. The helper
validates and snapshots that image, then calls the installed updater. All
worktrees use the same per-user endpoint; no image is staged in another checkout.
It does not expose arbitrary commands, targets or uploader flags.

Build and inspect the image first, close serial captures, request bootloader
entry, verify device identity, then upload with the digest. Let the upload finish.
Investigate write failures before retrying, and verify application status and
saved setup data after USB returns. Firmware flashing and setup transfers are
separate operations; the helper does not grant serial-transfer permissions.

The [target-specific helper guide](https://github.com/rec/cntrl/blob/main/cntrl/maker/morningstar/mc/firmware/upload-helper.md)
contains the implementation link, exact commands, device identity, image path,
validation rules, startup and recovery instructions.
