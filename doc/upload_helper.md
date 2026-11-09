# Firmware upload helper

Firmware upload helpers belong to the selected implementation and run through
its Python module. The common CLI remains usable without implementation code.
Use the implementation's helper guide for its entry point and command arguments.

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
