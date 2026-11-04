# Firmware upload helper

Use the running, user-launched upload helper for firmware flashing. Reuse that
process; do not start a second server. It accepts an upload request for a fixed
build image with an exact SHA-256, validates and snapshots the image, then calls
the installed updater. It does not expose arbitrary commands or image paths.

Build and inspect the image first, close serial captures, request bootloader
entry, verify device identity, then upload with the digest. Let the upload finish.
Investigate write failures before retrying, and verify application status and
saved setup data after USB returns. Firmware flashing and setup transfers are
separate operations; the helper does not grant serial-transfer permissions.

The [target-specific helper guide](../cntrl/maker/morningstar/mc/firmware/upload-helper.md)
contains the implementation link, exact commands, device identity, image path,
validation rules, startup and recovery instructions.
