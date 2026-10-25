# Upload-only helper

The patch-switcher application has repeatedly booted and communicated over USB,
including software-triggered uploads of the current format 9 firmware. See the
[deployment record](patch-switcher.md#current-deployment-2026-10-25).
Direct HalfKay uploads remain blocked by the
command sandbox. This helper is a separate, explicitly user-launched capability;
it does not change Codex permissions or expose general HID access.

## Start once in Terminal

Review `cntrl/maker/morningstar/mc/upload_helper.py`, then run:

```bash
cd /Users/tom/code/cntrl
uv run --frozen -m cntrl.maker.morningstar.mc.upload_helper serve
```

Keep that Terminal open. Do not use `sudo`. The helper must be launched by you,
not inside Codex's sandbox, otherwise it will inherit the same HID restriction.
No HID operation occurs at startup. It listens on the owner-only Unix socket
`/Users/tom/code/cntrl/cntrl/maker/morningstar/mc/firmware/.upload-helper.sock`.
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

After building and inspecting the image, close any serial monitor and request
bootloader entry from the running application:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.upload_helper bootloader
```

This local command opens only `/dev/cu.usbmodem144682801` and selects 134 baud.
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
uv run --frozen -m cntrl.maker.morningstar.mc.upload_helper upload --sha256 DIGEST
```

Codex runs this client command, not the server. `DIGEST` is the SHA-256 of
`cntrl/maker/morningstar/mc/firmware/build/patch-switcher.hex`, obtained immediately
before the request.
The protocol accepts only that digest as a newline-terminated JSON object of
at most 256 bytes. Unknown fields are rejected. No filename, target, executable
or extra uploader flags can be supplied through a request.

The helper then:

1. Copies the fixed project HEX into a private temporary directory under
   `~/Library/Caches/cntrl-upload/`, outside the current Codex writable roots.
   The uploader uses this snapshot, never the mutable build output.
2. Checks its digest, Intel HEX checksums and contiguous address range, matching
   MC6 Pro flash configuration, boot header, Thumb reset entry and zero CSF.
   Images cannot extend into the reserved `0x607c0000` flash tail.
3. Calls only the installed official updater's fixed `tycmd` executable.
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
