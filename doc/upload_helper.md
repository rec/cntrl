# Firmware upload helper

Use the running `cntrl.maker.morningstar.mc.upload_helper` for firmware uploads.
It lets Codex request USB flashing through a user-launched process with access
to the pedal. Do not launch another server when the helper is already running.

The implementation is
[`upload_helper.py`](../cntrl/maker/morningstar/mc/upload_helper.py).
It accepts requests over the owner-only Unix socket
`cntrl/maker/morningstar/mc/firmware/.upload-helper.sock`. It snapshots the fixed
image `cntrl/maker/morningstar/mc/firmware/build/patch-switcher.hex`, verifies its
requested SHA-256, HEX checksums, boot headers and known-good flash configuration,
then invokes the installed Morningstar updater's `tycmd` for pedal serial
`14468280`. The image must exclude the reserved flash tail at `0x607c0000`.
Requests cannot supply another image path, executable or uploader arguments.

## Upload workflow

Run from the repository root after building and inspecting the intended image:

```bash
shasum -a 256 cntrl/maker/morningstar/mc/firmware/build/patch-switcher.hex
uv run --frozen -m cntrl.maker.morningstar.mc.upload_helper bootloader
```

Close serial captures before bootloader entry. The `bootloader` command directly
selects 134 baud on `/dev/cu.usbmodem144682801`; it does not use the helper socket
or flash anything. Verify the expected pedal appears as HalfKay, then request
the upload using the digest printed above:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.upload_helper upload --sha256 DIGEST
```

The running server independently checks the HalfKay identity before flashing.
Let an upload finish without interrupting the helper. Afterward, verify that
application USB returns and check its status. The updater can report exit status
1 after a successful reset/disappearance, so check the device before deciding
whether an upload failed. A write error needs investigation rather than being
treated as a successful reset.

The helper handles firmware flashing only. Normal USB serial queries and setup
transfers use the application's serial connection directly and may still need
host permission. The helper does not grant those permissions.

If the server is absent, the user starts it in Terminal with
`uv run --frozen -m cntrl.maker.morningstar.mc.upload_helper serve`.
See the [full helper documentation](../cntrl/maker/morningstar/mc/firmware/upload-helper.md)
for startup, recovery and implementation details.
