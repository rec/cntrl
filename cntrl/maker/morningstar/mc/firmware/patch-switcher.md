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

## Read-only storage information

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.storage_info --port /dev/cu.usbmodem144682801
```

This reports physical card capacity, SD/SDHC/SDXC type, manufacturer ID,
OEM ID, product name, revision, serial number, manufacturing month,
filesystem type, allocation-unit size and usable filesystem capacity. Output is
TOML, including every file and directory recursively, with directory totals
ordered by estimated file allocation size.
Use `--output PATH` to save the same TOML report instead of printing it.
Identification comes from the card's CID register; manufacturer IDs are reported
as numbers rather than guessed brand names. SDXC is distinguished from SDHC by
capacity. Usable capacity counts allocation clusters, not partition metadata.

The command always scans free space and lists the whole card. This diagnostic
can block the main loop while reading the allocation table/bitmap and streaming
the directory tree, so do not run it while performing. It does not read file
contents, write files, change settings or send MIDI.

Each entry includes its path, file/directory flag, logical byte size and estimated
allocation rounded up to the allocation-unit size. Directory totals count files
recursively, excluding directory storage itself. `other_allocation_bytes` is
filesystem-used space minus estimated file allocation, not proof of lost space:
directories, filesystem bookkeeping and file preallocation can contribute.

The USB commands are `CNTRL STORAGE` and `CNTRL STORAGE SCAN`. The single-line
response is `CNTRL STORAGE SECTORS TYPE CID_HEX FAT CLUSTER_BYTES CLUSTERS FREE`.
Sectors are 512 bytes, CID is 16 bytes in register order, and FREE is a cluster
count or `-` when not requested. FAT is 16, 32 or 64 (exFAT). `SCAN` then streams
`CNTRL ENTRY ID PARENT D_OR_F SIZE_HEX NAME_HEX` for each entry and ends with
`CNTRL STORAGE END COUNT`. Root has ID 0; entries have consecutive IDs starting
at 1, with parents preceding children. Sizes use 16 hex digits, preserving
64-bit exFAT sizes; hex-encoded names preserve spaces.
The host rejects partial reports and reconstructs paths and totals on the Mac.
The short `CNTRL STORAGE` metadata-only query remains unchanged. Errors use
`CNTRL ERROR storage-unavailable`, `storage-read`, `storage-scan` or `storage-list`. Commands
are handled only when no show/animation transfer or verification is active.

Verified on the pedal, 2026-10-23:

- Capacity: 31,914,983,424 bytes (nominal 32 GB), SDHC.
- Manufacturer ID `0x03`, OEM `SD`, product `SP32G`, revision 8.0;
  manufactured September 2024.
- exFAT, 32,768-byte allocation units; usable 31,905,546,240 bytes.
- Free 30,723,670,016 bytes; used allocation 1,181,876,224 bytes.

Both default and opt-in scan commands succeeded over USB. The active show
remained `CNTRL READY 4 22242 9afa8de1` before and after firmware upload and
queries. The inspected/uploaded HEX SHA-256 was
`1f7a4b75bdedd44d6b678b24041b0a796c9b1975a9fc762fb4860767254a30ae`.
The uploader returned status 1 after reset/disappearance, as in earlier uploads;
the new command's successful responses established that the new firmware booted.

The recursive TOML report was verified on the pedal on 2026-10-23. It found
34,919 files with 76,083,789 bytes of content, requiring an estimated
1,146,880,000 allocated bytes at 32 KiB per allocation unit. `/PF` alone had
34,831 files, 72,480,824 logical bytes and 1,141,342,208 allocated bytes. Its
numbered subtrees contain bank/preset `.dat` files; these paths suggest factory
profile storage, but this query does not inspect their contents.

The 1,057 listed directories account for an estimated 34,635,776 further bytes.
The remaining difference from filesystem-used space is 360,448 bytes, not
independently attributed. AppleDouble `._` files account for 3,073 of the files
and 100,696,064 allocated bytes, already included in the totals above.
No files were changed or removed. The active show remained
`CNTRL READY 5 22246 bf83dc72` before and after upload/queries.
The inspected/uploaded HEX SHA-256 was
`777c5a23a655fa46bb90dd04adcb7b813a1f7d407bc10ad044d2b8d573ebe90f`.
An initial live test exposed unsupported `%llu` formatting in the compact target
C library; the protocol now uses two 32-bit hex halves for 64-bit file sizes.
The complete generated report is saved locally as
`firmware/build/storage-report.toml` under this directory, not committed.

## Adapter and storage boundaries

Show format version 6 includes shared top-level `font_name`/`font_size`/`pad` and
`screens.undo`, `screens.sent`, `screens.next` colors, including optional `half`.
Only the rightmost patch name uses `half` when it matches the last successful
send for its instrument (program and bank select, not label/category). Without
an explicit `half`, the Mac computes the RGB midpoint of foreground/background.
Version 6 has been built and tested, not flashed. Existing shows need recompiling.
Brightness
(0..255, default 192) applies to backlight PWM pin 6 on show activation, including
startup. Older binary shows are rejected; recompile the original show and upload
it after updating firmware. Top-level `splash` defaults to false; when true, the
installed animation plays until any button is pressed. See the
[splash instructions](../splash/README.md) for preparation and upload. Splash
images fill the panels without text padding. The dismissing gesture sends no
MIDI, and all normal UI pixels are redrawn afterward.

`screensaver_timeout` sets idle seconds (0 disables, the default). The pman test
show uses 3 seconds. Entry fades through black with 4,000 ms per half; waking
uses 200 ms per half and performs the waking button's normal action immediately.
Both transitions wait in black for a complete repaint. Held buttons prevent
entry; presses and releases restart the timer. Only startup dismissal consumes
its triggering gesture. Show or asset transfers cancel playback and inhibit
idle entry until they finish; ordinary USB queries do not count as physical input.

USB exposes Serial + MIDI, using the public core's development VID/PID and the
existing `cntrl experimental` product string. The serial receiver accepts show
documents, not HEX or firmware. There is no new application-flash writer and no
factory EEPROM access. The existing PROGRAM recovery path is retained by using
the verified startup and flash configuration; this image still needs a physical
boot and recovery check after its first authorized upload.

The adapter uses the traced native SD interface with the pinned SD/SdFat
libraries. It requires an existing readable FAT/exFAT card. It does not format a
card, migrate vendor data or probe the EEPROM. Show storage owns two filenames at
the card root: `cntrl-show-0.bin` and `cntrl-show-1.bin`. Those names must be
reserved for cntrl. Upload replaces only the inactive cntrl file, writes a commit
trailer last, flushes/closes it and reads it back before activation. Boot ignores
partial or corrupt slots and selects the newest valid generation. Splash assets
use separate `cntrl-splash-0.bin` and `cntrl-splash-1.bin` slots with their own
validation and commit markers.

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
through the pinned ST7735_t3 library's RAM framebuffer and font API.
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

The configurable-graphics ARM link used approximately 309 KiB for RAM1 variables
and 96 KiB for code plus ITCM padding, leaving approximately 107 KiB for local
variables. Exact link measurements are printed by `inspect-patch-switcher`.
RAM2 static use was 17,568 bytes; no external RAM was allocated. Final link
measurements are printed by `inspect-patch-switcher` and may vary as code changes.

The current allocation supports two 65,536-byte document buffers, 32 routed
instruments, 128 categories and 128 Undo entries. Record sizes impose an upper
bound of 796 patches for the smallest possible instrument/category tables;
larger tables reduce this through the document byte-size check. Instrument and
output IDs occupy at most 31 printable ASCII characters; category/patch names
occupy at most 63. The compiler chooses one shared fitting font size; category
and patch names are centered on separate lines using saved cursor offsets,
without runtime width measurement, wrapping or font-size changes. These are the
implemented build's limits, not claims about maximum
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
font APIs are available on `Graphics`; selectable fonts are now show options.
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

## Show graphics configuration

See [show configuration](../../../../../firmware/README.md#screen-graphics).
Every screen displays only its category and patch, not a title or instrument.
`font_name`, `font_size` and `pad` are shared by all three screens. Only colors
remain independent. The shared padding determines the common available
width/height for automatic sizing; the selected font must fit both lines there.
Foreground/background color selection and warnings are resolved on the host.
Rendering fills the whole background and clips all text to the padded rectangle.

Fonts are `classic` (default), `Arial`, and `ComicSansMS`. Classic sizes are 8,
16, ... 96 (6x8 cell scaled by an integer). Native font sizes are 8, 9, 10, 11,
12, 13, 14, 16, 18, 20, 24, 28, 32, 40, 48, 60, 72 and 96; these are the
library's nominal size names. Unsupported sizes are errors, not silently rounded.
Automatic sizing picks the largest supported size fitting every name and both
lines. The selected size is resolved once on the Mac, never searched on the pedal.

`build_font_metrics.py` exports advances, left bearings, widths and vertical
extents from the pinned library into `patch-switcher-target.json`:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.build_font_metrics
```

Packed glyph metrics in the target file are generated data. The document stores
only selected font IDs/sizes, RGB565 colors, padding, vertical metrics and
precomputed name offsets, not glyph bitmaps. All supported font variants remain
in the generic firmware so future shows can change fonts without reflashing.
`fonts-in-flash.patch` places the library's immutable native font data in mapped
program flash instead of DTCM. It does not change font contents or decoding.
Native tests link the same library font tables; their graphics test double tests
layout scheduling, clipping, color byte order and erasure, not actual font shapes.

Python compilation tests cover default/omitted/null settings, shared sizing
with shared padding, named/hex colors, contrast warnings and fallback,
per-screen settings/offsets, proportional font metrics and accumulated errors.
The receiver checks font support, a common font/size/padding in all three records,
padding/height, offsets and reserved bytes before activation.

## Integrated splash: 2026-10-23

Show format 4 enables the separately installed animation with `splash: true`.
The Mac prepares full-bleed RGB565 rows, including the mirrored centre panel;
the target streams one row pair per poll and broadcasts the outer rows. Button
scanning, USB and output queues remain serviced. The dismissing gesture is
consumed, then all normal renderer caches are invalidated for a complete repaint.

All 135 Python tests, portable controller/persistence tests, target display and
renderer tests, and splash runtime/persistence tests passed. Ruff, ty, pyupgrade
and diff checks passed. The inspected HEX matched the ELF and stock flash
configuration. Its SHA-256 is
`27c176c6e831ddee191cf6d2d012f38347f503b0e5cfc75a231d434ca4fd8f37`.
Flash use is 379,900 bytes; RAM1 has 104,320 bytes free and RAM2 has 506,720 free.

Software bootloader entry and helper upload succeeded. The updater returned
status 1 after reset/disappearance; subsequent USB status confirmed version 4
running. The animation upload verified as
`CNTRL SPLASH READY 1310800 4b7e5ee5`, followed by the recompiled pman show:
`CNTRL READY 4 22242 e39424b7`. Visible orientation, frame rate, clean dismissal
and cold-boot persistence still require physical confirmation. No physical
power-loss or interrupted-write qualification is claimed.
