# Upload-only helper

The patch-switcher application has repeatedly booted and communicated over USB
after uploads through this helper. Firmware and compiled setup data are separate;
verify both the application boot and the selected setup after an upload.
Direct HalfKay uploads remain blocked by the
command sandbox. This helper is a separate, explicitly user-launched capability;
it does not change Codex permissions or expose general HID access.

## Start once in Terminal

Review `cntrl/maker/morningstar/mc/upload_helper.py` and `updater_provision.py`.
Provision the pinned official TyTools 0.9.9 uploader once, without accessing the
pedal:

```bash
cntrl upload-helper --target-dir cntrl/maker/morningstar/mc provision
```

On macOS this verifies the release DMG, mounts it read-only, and copies the
verified executable into `~/Library/Caches/cntrl-upload/tytools-0.9.9/tycmd`.
Windows uses the official ZIP and installs `tycmd.exe` under
`%LOCALAPPDATA%/cntrl-upload/tytools-0.9.9/`. Both the archive and executable
have pinned SHA-256 checksums. A matching installation is left untouched.
The Morningstar updater application is not changed.

Then run in Terminal:

```bash
cd /Users/tom/code/cntrl
cntrl upload-helper --target-dir cntrl/maker/morningstar/mc serve
```

Keep that Terminal open. Do not use `sudo`. The helper must be launched by you,
not inside Codex's sandbox, otherwise it will inherit the same HID restriction.
The default updater is the provisioned executable, whose checksum is verified
before use. Set `CNTRL_TYCMD` to select an explicitly managed executable instead.
Linux requires this override because the pinned release has no Linux archive.
An unset or empty `CNTRL_TYCMD` selects the pinned installation. Missing or
modified installations stop the helper before device access.
No HID operation occurs at startup. On macOS it listens on the owner-only Unix
socket `~/Library/Caches/cntrl-upload/helper.sock`. The manually launched server
creates an authentication key there; clients only read it. Linux uses
`$XDG_CACHE_HOME/cntrl-upload/` (default `~/.cache/cntrl-upload/`); Windows uses
the named pipe `\\.\pipe\cntrl-upload-14468280` and a key under
`%LOCALAPPDATA%/cntrl-upload/`.
There is no TCP listener,
background installation, automatic upload, reset command or general execution
endpoint. Stop it with Ctrl-C **only when an upload is not in progress**.

The source and dependencies must be trusted when you start the helper. Review
changes before restarting it. This is a trusted process under your account,
not a kernel-enforced HalfKay-only sandbox. Other processes under your account
can also send requests to its socket. An untrusted local administrator or a
spoofed USB device is outside this helper's protection model.

If startup reports an existing socket, stop and check for an already-running
helper. It deliberately does not delete or replace an existing socket. An
abnormal termination can leave a stale socket; diagnose that before removing it.

## Request an upload

For a complete build, backup, firmware installation and verified data restore,
use the public command from the repository root:

```bash
cntrl flash --target-dir cntrl/maker/morningstar/mc --output cntrl/maker/morningstar/mc/firmware/build/device-flash --port /dev/cu.usbmodem144682801
```

The upload helper must already be running in your Terminal. Use a fresh output
directory for each installation. The command retains the immutable backup in
`before/` and validates its current-format data before asking the pedal to
reboot. Add `--legacy-usb` only when the installed firmware uses
the old text-framed USB protocol. This uses the maintained MC backup adapter
inside cntrl. The command restores and verifies the exact saved data without
format conversion. Incompatible backups stop the operation before reboot.

The final SD scan can find obsolete movie files that the installed firmware
cannot read. Those paths are reported and retained in `unavailable-assets.txt`;
the files on the pedal are left untouched. Failure to download an asset used by
a current setup or history revision still stops the operation before reboot or
upload.

The MC build entry provisions the existing pinned FreeType recipe before
loading its font-generation helpers, so a fresh dependency installation also
uses the required rasterizer when invoked through `cntrl`.

The helper's image validation, fixed device identity, snapshotting and sole
uploader capability remain in force. Upload is not retried automatically. A
nonzero uploader exit after reset is followed by application/data verification
when the device returns, but remains an error because data verification alone
does not prove that the new firmware was written. Success requires both a
successful uploader result and matching selection, setups, history, assets and
song library. The build export and all backups remain
under the selected output directory. They are generated artifacts and must not
be committed, particularly the combined image containing stock firmware.

After building and inspecting the image, close any serial monitor and request
bootloader entry from the running application:

```bash
cntrl upload-helper --target-dir cntrl/maker/morningstar/mc bootloader
```

This local command discovers the serial port for pedal serial `14468280` and
selects 134 baud.
The bundled Teensy USB core treats that line-coding request as bootloader entry;
it is not an application MIDI message or saved setting. No new firmware handler
is needed. The command does not flash anything or claim that HalfKay has appeared.
It runs directly, not through the helper socket, so an existing helper does not
need restarting. Stop playback and finish document uploads before using it.

Verify that the expected pedal enumerates as HalfKay before requesting upload.
The upload helper independently checks its identity and mode. If application USB
is unavailable, briefly press PROGRAM instead. Physical PROGRAM recovery remains
necessary for broken firmware that cannot process the USB request.

Request the upload with the exact image digest:

```bash
cntrl upload-helper --target-dir cntrl/maker/morningstar/mc upload --image /absolute/path/to/firmware.hex --sha256 DIGEST
```

Codex runs this client command, not the server. `DIGEST` is the SHA-256 of the
specified file, obtained immediately before the request. Each request chooses
its own image, so the shared helper can serve different agents and worktrees
without copying their output into the primary checkout. The old `prepare`
staging command is removed.

The image must be an absolute path whose resolved location is a `.hex` file
inside one of this repository's registered Git worktrees. Symlinks leading
outside those worktrees are rejected. The server discovers worktrees using
`git worktree list`. The per-user endpoint and authentication key are shared
regardless of which worktree runs the client or server. Git must be on the
server's PATH.
The protocol accepts only the image path and digest as a newline-terminated
JSON object of at most 8192 bytes, allowing long workspace paths. Unknown fields
are rejected. No target, executable or extra uploader flags can be supplied.

An already-running server must be stopped when idle and manually restarted with
the updated source before using this protocol. Clients must also use the updated
source. Review and start the updated source as described above; do not start a
second server or replace a live socket.

The helper then:

1. Copies the requested worktree HEX into a private temporary directory under
   `~/Library/Caches/cntrl-upload/`, outside the current Codex writable roots.
   The uploader uses this snapshot, never the mutable build output.
2. Checks its digest, Intel HEX checksums and contiguous address range, matching
   MC6 Pro flash configuration, boot header, Thumb reset entry and zero CSF.
   Images cannot extend into the reserved `0x607c0000` flash tail.
3. Calls only the server's updater executable, using `CNTRL_TYCMD` when set or
   the checksum-verified, provisioned TyTools executable.
   A read-only listing must identify exactly the expected board tag, serial
   `14468280`, HalfKay description, Teensy 4.1 model and upload capability.
4. Uploads with compatibility checks enabled and the fixed board selector.
   The uploader may restart the application afterward. Its output and exit
   status are returned unchanged; there are no automatic retries.

Do not cancel an upload in progress. A lost client connection does not itself
cancel the uploader. There is deliberately no subprocess upload timeout that
could kill a flash operation midway. Keep the stock image, bank backup and
PROGRAM access available.

An uploader error does not necessarily mean nothing was written: the official
updater previously reported failure even though our custom USB identity appeared
and the image booted. Check enumeration and heartbeat separately after upload.
The helper does not automatically monitor serial, enter the bootloader or
restore stock firmware.

## Limits and verification

The envelope checks are not a proof that uploaded instructions are safe. A
malicious or broken firmware with valid headers can still damage settings,
drive hardware incorrectly, or write irreversible security configuration.
This helper limits the host operation and target, not firmware behavior.

The staging path is safe from the agent only while it remains outside its
writable roots. Do not add `~/Library/Caches/cntrl-upload/`, its parents, or
the uploader application to those roots. No configuration change is needed
for this helper. Unix-socket access from this desktop execution route and HID
access from the manually launched helper have both been verified on the pedal.

Focused tests use a fake uploader. They cover request rejection, boot-envelope
checks, digest mismatch before device access, wrong-target rejection, immutable
staging, fixed uploader arguments and propagation of its output and status.
No helper service or hardware upload is started by the test suite.

## Software bootloader verification: 2026-10-23

The running SPI/dirty-row image responded with
`CNTRL READY 1 20568 bb8baa89`. The local `bootloader` command then selected
134 baud, and the official updater's read-only listing identified serial
`14468280`, tag `14468280-Teensy`, description `HalfKay`, model `Teensy 4.1`,
with upload capability. No PROGRAM press or firmware change was needed to enter
the bootloader.

The subsequent upload request could not connect because the previously launched
helper had stopped. Its socket had no owner and refused connections, so it was
moved to `.upload-helper.sock.stale-20261023` without deletion. After the user
restarted the helper, the same inspected SPI/dirty-row image was uploaded:
SHA-256 `13e18a18f090880a3455464c56785aecf6fc3974ab82e88c2b3a536c14a82fa7`.
The uploader returned status 1 after reset/disappearance, as on previous working
uploads. The application then responded with `CNTRL READY 1 20568 bb8baa89`,
confirming reboot and reload of the saved pman show. This completes the live
software-entry/upload/reboot verification without a PROGRAM press.

All 99 Python tests passed, including a real pseudo-terminal test checking that
the command selects 134 baud without writing a serial payload or changing other
terminal settings. Ruff, formatting, type checking and pyupgrade also passed.

## Verify after upload

An I/O write error is a failed upload attempt. Recheck the expected HalfKay
identity and the unchanged image digest before a deliberate retry. Let every
attempt finish; the helper never retries automatically. A later reset followed
by board disappearance can return status 1 even when flashing succeeded.
Check that application USB returns, query `CNTRL STATUS`, and verify the saved
selection and setup bytes. USB enumeration alone does not verify setup data or
physical MIDI behavior. Recompile and restore valid saved setups if their compiled
format changed, preserving backups and already-invalid documents.

## Uploader identity investigation: 2026-11-14

The bundled `0.9.0-9-g058715b` executable classified the running pedal
(`16c0:0489`, serial `14468280`) as Generic, with tag `14468280-cntrl` and only
unique/serial capabilities. The pinned official 0.9.9 executable recognizes
the same application as Teensy, tag `14468280-Teensy`, with unique/run/reboot/serial
capabilities. Its diagnostic warning about the custom `bcdDevice` value does not
prevent that classification. This restores the application/bootloader identity
needed for post-upload monitoring; a successful physical upload is still required
to verify the reset path and investigate the initial HID write errors.

Use `cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --updater-info
--report PATH.toml` for a read-only comparison. It records the executable digest,
version, upload/reset help, debug enumeration, relevant raw USB match records and
existing TyTools configuration files. It neither resets nor flashes the pedal.
