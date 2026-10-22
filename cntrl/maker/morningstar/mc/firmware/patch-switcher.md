# MC6 Pro patch-switcher build

This is the first complete application build, not a hardware-qualified release.
The first patch-switcher firmware and pman show were uploaded on 2026-10-22.
See the live verification record below for confirmed results and remaining checks.

```bash
make -C firmware test
uv run --frozen pytest
make -C cntrl/maker/morningstar/mc/firmware test inspect-patch-switcher
```

The build creates `build/patch-switcher.hex` in this directory, plus the ELF and
raw image under `build/patch-switcher/`. The inspect target compares HEX and ELF
bytes and checks the complete stock flash configuration. Neither target invokes
an updater. The diagnostic `usb_probe` build remains available for hardware
investigation; it shares button scanning and display transactions with the new
adapter, not the patch-switcher application logic.

Use the generic [host instructions](../../../../../firmware/README.md) with
`--target cntrl/maker/morningstar/mc/patch-switcher-target.json`. The Python CLI
and compiled show format are independent of the controller. Install firmware
separately using the established recovery/upload workflow only when authorized.

## Adapter and storage boundaries

USB exposes Serial + MIDI, using the public core's development VID/PID and the
existing `cntrl experimental` product string. The serial receiver accepts show
documents, not HEX or firmware. There is no new application-flash writer and no
factory EEPROM access. The existing PROGRAM recovery path is retained by using
the verified startup and flash configuration; this image still needs a physical
boot and recovery check after its first authorized upload.

The adapter uses the traced native SD interface with the pinned SD/SdFat
libraries. It requires an existing readable FAT/exFAT card. It does not format a
card, migrate vendor data or probe the EEPROM. It owns exactly two filenames at
the card root: `cntrl-show-0.bin` and `cntrl-show-1.bin`. Those names must be
reserved for cntrl. Upload replaces only the inactive cntrl file, writes a commit
trailer last, flushes/closes it and reads it back before activation. Boot ignores
partial or corrupt slots and selects the newest valid generation.

The stock firmware's native-card path is traced, and a show upload has completed
with write/readback verification against the installed card. FAT directory updates and SD-card internal
caching mean simulated interrupted writes are not proof of physical power-loss
safety. Qualification must check cold boot, two different uploaded shows,
interrupted transfers and interrupted commits. If there is no readable card,
the application displays an error, sends no MIDI and rejects persistent uploads.

Output IDs are `din`, `trs`, `configurable-1` through `configurable-4`,
`usb-device` and `usb-host`. DIN uses Serial1 and the dedicated 3.5 mm output
uses Serial2, both 31250 baud. Configurable ports currently provide the traced
standard/type-A output mode: tip TX, ring HIGH. Type B, ring-active and expression
modes are not advertised by this build. Only use appropriate MIDI cables/devices
on those ports; this application does not import saved port settings.

FlexSerial outputs are initialized when first used. USB host uses the core's
host stack and one MIDI device; USB device uses MIDI cable 0. Other USB classes,
multiple host devices/hubs and input forwarding are not implemented. A complete
patch sequence is admitted to a 32-entry output queue before history changes.
USB disconnects or later transport failures cannot be acknowledged by a MIDI
receiver and are not equivalent to a confirmed instrument state change.

Display slots currently follow the traced CS order. Their left/center/right
physical order still needs verification. The three 160x128 drivers render the
portable layout using the pinned public-core ASCII font. Runtime updates write
eight-pixel segments, deselecting displays before button scanning resumes.
Rendering snapshots the latest requested pane and coalesces further requests.
No full framebuffer is allocated. Verify redraw speed and button latency on the
pedal; SD and USB library calls can introduce latency beyond the bounded drawing
transactions.

## Capacity measurements

The font-data-only ARM link used 154,208 bytes for RAM1 variables, 65,536
bytes for code plus ITCM padding, leaving 304,544 bytes for local variables.
RAM2 static use was 17,568 bytes; no external RAM was allocated. Final link
measurements are printed by `inspect-patch-switcher` and may vary as code changes.

The current allocation supports two 65,536-byte document buffers, 32 routed
instruments, 128 categories and 128 Undo entries. Record sizes impose an upper
bound of 860 patches for the smallest possible instrument/category tables;
larger tables reduce this through the document byte-size check. Instrument and
output IDs occupy at most 31 printable ASCII characters; category/patch names
occupy at most 63. The renderer wraps names and reduces patch scaling when
needed. These are the implemented build's limits, not claims about maximum
hardware capacity. The target JSON records them for the compiler.

The two slots consume at most 131,112 file bytes, excluding filesystem overhead.
The installed card's free space is unknown until hardware qualification. A full
or failing card must leave the old committed slot usable and produce an upload
error; that path is covered with fake storage but not yet physically tested.

## First live upload: 2026-10-22

- Firmware HEX SHA-256:
  `1d350bc7cb018452e835c7fa5f73c0a880ddc5440443023ef8c963eaace9b4bb`.
- The upload-only helper selected the expected HalfKay board and uploaded the
  81 KiB image. The official uploader returned exit status 1 after sending reset
  and reporting that the board disappeared. USB serial nevertheless returned
  `CNTRL READY 1 0 00000000`, confirming the new application booted.
- Uploaded the compiled `examples/patch-switcher/pman.json` show: 256 patches,
  15 categories, pman channel 1, `din` output. The receiver completed its durable
  commit/readback path and returned `CNTRL READY 1 20568 bb8baa89`.
- Captured DIN output through CME U6MIDI Pro input 1 while the user performed
  C, B, C, F. Received exactly three sequences on channel 1: bank MSB 33,
  bank LSB 0, then programs 1, 2 and 1 respectively (human numbering).
  This verifies send, browsing without an extra send, and Undo restoring Trumpet
  after Maynard. The capture reported nine messages, no loss and no failure.
- Cold-boot persistence, recovery from this image, physical display ordering
  and power-loss behavior remain unverified at this point.
