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

Show format version 2 adds top-level `brightness` (0..255, default 64), applied
to backlight PWM pin 6 on show activation, including startup. Older binary shows
are rejected; recompile the original show and upload it after updating firmware.
The first version 2 upload is recorded in the library-graphics verification below.

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
physical order still needs verification. The three 160x128 drivers render
through the pinned ST7735_t3 library's RAM framebuffer and classic font API.
Transfers use blocking hardware SPI at a requested 8 MHz, MSB first, mode 0.
Initialization retains the
traced panel commands, reset sequence and waits. Each runtime call processes at
most one library glyph or one row and sends only the span between a row's first
and last changed pixels. Unchanged rows generate no SPI traffic. One 40,960-byte
RGB565 canvas is shared across panels, and a 122,880-byte cache records the pixels
sent to all three panels; the first pass clears unknown panel RAM.
Rendering snapshots the latest requested pane and coalesces further requests,
including changes received during painting and erasure of shorter text.

All panels are deselected before ending a transfer. Pin 13 is then restored to
GPIO output LOW and pin 12 to GPIO input for the existing switch-register reader.
The next transfer restores the SPI pin mux through `SPI.begin()`. No asynchronous
display transfer or display interrupt masking crosses a button scan. The largest
row transaction sends 331 bytes, approximately 0.34 ms of wire time at 8 MHz,
excluding CPU/peripheral setup. Verify redraw speed and button latency on the
pedal; SD and USB library calls can still introduce latency. The previous
software-clocked driver had an observed update time around 1,500 ms.

## Capacity measurements

The library-graphics ARM link used 316,736 bytes for RAM1 variables, 98,304
bytes for code plus ITCM padding, leaving 109,248 bytes for local variables.
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

## Hardware SPI and dirty-row upload: 2026-10-22

- Firmware HEX SHA-256:
  `13e18a18f090880a3455464c56785aecf6fc3974ab82e88c2b3a536c14a82fa7`.
- Portable controller/persistence tests and target button, display-transfer and
  renderer tests passed. Renderer tests compare the simulated panel contents
  against the requested text, including shortened/cleared text and replacement
  requests during painting. They verify one-row transfer bounds, initial clearing
  and no transfers for unchanged content. SPI tests check transaction settings,
  exclusive panel selection and GPIO restoration before returning to scanning.
- The HEX/ELF comparison and stock flash-configuration check passed. Uploaded
  through the helper; the official updater again returned status 1 after reset
  and disappearance. The application responded over USB with
  `CNTRL READY 1 20568 bb8baa89`, confirming a boot with the existing saved show.
- Visible redraw speed and switch responsiveness still need the user's check.
  The first post-upload MIDI capture returned ten events almost simultaneously,
  not the requested timed sequence; buffered interface data cannot establish
  current button latency or confirm that sequence.

## Library graphics and fonts: 2026-10-23

`patch_switcher/graphics.h` wraps the pinned ST7735_t3 class as a 160x128 RAM-only
canvas. It supplies its own RGB565 buffer, enables framebuffer mode and uses the
library's `setFont`, `setTextSize`, `setCursor`, `write` and `fillScreen` APIs.
The library now owns glyph decoding and rasterization. The old portable
`layout.h` pixel-font engine has been removed. Existing field spacing, wrapping,
classic font and patch-name size reduction are retained.

Only the framebuffer graphics paths are used: no `initR`, hardware rotation,
`updateScreen` or asynchronous flush is called. Drawing primitives and native/GFX
font APIs are available on `Graphics`; selectable fonts are not yet show options.
The library's static initialization reserves three inactive DMA channels, but no
display DMA transfers are started. Our driver still owns panel initialization,
chip selects, SPI timing, coordinate offsets and the shared GPIO/SPI pin handoff.

Drawing is incremental: at most one glyph or one row per poll. Native tests use
an explicit graphics test double to test scheduling, erasure, wrapping, change
coalescing and per-loop bounds, not to claim verification of library font shapes.
The real pinned library is compiled into the ARM image and checked on the pedal.
Portable controller/persistence, target buttons, display transfer and renderer
tests passed, including address/undefined-behavior sanitizer checks.

The image SHA-256 is
`d53f49bd5225846fec6629cf833140745c49946e3db3a12027ba69993772089e`.
HEX/ELF bytes and stock flash configuration matched. Software bootloader entry
and helper upload succeeded without PROGRAM. The updater again returned status 1
after reset/disappearance; the application then replied `CNTRL READY 2 0 00000000`.
The recompiled pman show uploaded and verified as
`CNTRL READY 2 20568 c7d6e19f`, including default brightness 64.
The user confirmed the text, update speed and button response looked unchanged.
The CME input-1 capture ended with the expected channel-1 bank MSB 33/LSB 0,
program 1, program 2, program 1 sequence for C/B/C/F. It also contained earlier
buffered traffic, so it is not a latency measurement; no lost events were reported.
