# MC6 Pro patch-switcher build

The application is built and running on the pedal. It is not a fully
hardware-qualified release. Dated sections below retain earlier image hashes,
format numbers and measurements; they are historical, not the current build.

## Current deployment, 2026-10-25

- Deployed source: `48b2693` (`Add display_name overrides to patch_switcher`),
  including the latest independent centre/outer splash colour and speed walks.
- HEX SHA-256:
  `39f4c72d434e57791e1436a8ab53180228dba945a02ed872eb7087c1acaf2a1d`.
- Running status: `CNTRL READY 9 26866 36424cc1`.
- Selected setup: `pman`, 256 patches, 15 categories, channel 1, DIN output.
  The live version 8 setup was backed up and recompiled before flashing. All
  existing runtime records, labels, MIDI addresses and settings were unchanged;
  version 9 adds separate name metadata. The restored setup was downloaded and
  matched the prepared export exactly. No empty setup was substituted.
- The bridge is off in this setup. Enumeration exposes DIN and TRS inputs and
  DIN, TRS and four configurable outputs. The CME U6MIDI Pro also enumerates
  all three input/output pairs. Post-flash discovery/status sent no musical MIDI.
- Before upload: 215 Python tests, portable/native controller and MIDI bridge
  tests, button/display/renderer and splash/runtime tests passed. HEX/ELF and
  stock flash-configuration checks passed. The helper uploaded the image after
  software bootloader entry; no PROGRAM press was needed.

The latest build has 324,480 bytes of RAM1 variables and 98,304 bytes of RAM1
code/padding, leaving a 101,504-byte stack budget. RAM2 fixed allocation is
215,072 bytes, leaving 309,216 bytes for heap. Flash tool usage is 396,284 bytes
of the 8,126,464-byte application region. These are linker budgets, not measured
runtime peak stack/heap use. No external RAM is used.

Previously verified: all six buttons, text/orientation/panel ordering, fast
hardware-SPI redraws, software bootloader entry/upload/reboot and USB-to-DIN
short-message/SysEx output. The two physical inputs, TRS output and four
configurable outputs still need individual loopback tests. SD-card power-loss
safety, complete editor coverage and release qualification remain unproven.
New version 9 labels and the latest visual animation behavior need observation
on the pedal; successful boot and read-back are not a visual test.

## Sliding send/undo panes, 2026-10-24

C sends MIDI and pushes history before requesting a leftward transition. F
performs the existing per-instrument undo, selects the undone patch as R's
browsing candidate, and requests the reverse transition. Failed sends and an
empty undo stack do not request animation. There is no MIDI delay waiting for
graphics, and no persistent history or graphics-format change.

The screen pitch is 232 pixels: 160 visible plus a 72-pixel physical-gap
estimate. Over a show-configured duration at 60 Hz (default 1 second after preparation), the
old panes move left on C and right on F. The new R candidate is rendered using
the existing grey-name rule before entering from the right. On F, the next
undo target, or a blank pane, enters from the left. Virtual gaps are black.

The RAM-only library still renders fonts. Preparing destination bitmaps is
incremental and leaves the old screen contents visible. Native ST7735 column
writes use the tested 8 MHz byte-wise protocol and `MADCTL=0x88`; each animation
poll advances at most one pixel and writes one column per panel. Button scans,
USB and queued MIDI output run between these polls. Scroll commands are issued
at frame targets. Destination screen-specific styling is reconciled afterward.
The target duration is not a hard real-time guarantee under SD/USB stalls.

Ordinary UI repainting also uses native columns and retains the current ring
offset. Splash output switches back to landscape row addressing and resets the
scroll offset. On return, renderer invalidation restores native mode and
repaints behind the existing first-image/blackout gate. New actions during an
animation retain their MIDI/history effects and coalesce into the latest pending
view; they do not build an unbounded animation queue.

Verification: 194 Python tests, the sanitizer-enabled portable controller suite,
and native button/display/renderer tests passed. New tests cover forward/reverse
pane mapping, nonzero ring offsets, the native-column protocol and return to
splash addressing. Controller tests cover browsing away before undo and exclude
failed sends/empty undo from transition events. HEX/ELF comparison and the stock
flash-configuration digest passed. RAM1 has 111,680 bytes free, RAM2 309,312,
with no external RAM linked.

Uploaded main-image SHA-256:
`6baf5b03d9ff0de30d854c5fce4547f1e4c585084a8a388dd5dc2cce4cc05872`.
After the uploader's usual reset/disappearance status 1, USB confirmed format 7.
The setup catalogue was empty. The existing pman example was recompiled,
copied with readback verification and selected; status then reported
`CNTRL READY 7 23390 0718301d`.

At that deployment, `instruments/pman.toml` mixed an initial
`[[patches.pman.0]]` header with `[[patches]]` entries and lacked explicit setup
fields. It was not edited for that task. Upload used a temporary copy with the
flat initial header and `setup = "default"` on each entry, matching the example's
existing default-setup routing. No factory EEPROM or vendor backups were changed.
The current repository table now uses the flat header and explicit default setup.
Physical transition/splash/text verification remains pending user observation.

The user subsequently confirmed that the original transition build looked
correct. A timing fixup changes the default from 400 ms to 2 seconds. Set
`transition_ms = 2000` at the top level of a show to specify it explicitly, or
choose another integer duration in 1..65535 milliseconds. The compiler stores
the setting in the metadata header's previously reserved u16; zero denotes the
firmware default and preserves existing compiled shows and their checksums.
Resolved TOML exports retain this setting. Both send and undo receive the same
duration, without changing MIDI timing. The two-second fixup was uploaded and
boot-verified. A subsequent fixup sets the default to 1000 ms (one second);
explicit `transition_ms` values still take precedence.
The current implementation keeps an omitted duration unspecified through the
model, compiled metadata (zero), and firmware (`std::nullopt`). The renderer
alone resolves it to `DEFAULT_TRANSITION_MS`, currently 500 ms for both send
and undo, when starting a transition.
Resolved TOML omits unspecified durations; explicit values, including 1000,
remain explicit throughout.
The one-second image was flashed and boot-verified with pman and its checksum
unchanged. SHA-256: `b2c97ed563419a8a308193f7c104ad0150ea6af69ecfd1e3fadf423c42f03b2e`.

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

Historically, show format version 9 included shared top-level `font_name`/`font_size`/`pad` and
`screens.undo`, `screens.sent`, `screens.next` colors. Current firmware uses
foreground-color stippling for the rightmost patch name when it matches the last
successful send for its instrument (program and bank select, not label/category).
It also retains instrument identities, logical banks and bank mappings for
self-contained editable downloads. Version 9 preserves original synthesizer
names and the separate program/show `display_name` overrides in variable-length
metadata; rendering uses the resolved label. Downloads embed the used program
descriptions in `definitions.programs`. The default-off per-setup
`usb_midi_interface` switch remains available. See the [USB MIDI interface plan and qualification
record](../usb-midi-interface-plan.md). Existing shows need recompiling.
Brightness
(0..255, default 192) applies to backlight PWM pin 6 on show activation, including
startup. Older binary shows are rejected; recompile the original show and upload
it after updating firmware. Top-level `splash` defaults to true; explicit false
disables startup playback. With no selected setup, boot also uses the installed
animation. When enabled, the
installed animation plays until any button is pressed. See the
[splash instructions](../splash/README.md) for preparation and upload. Splash
images fill the panels without text padding. The dismissing gesture sends no
MIDI, and all normal UI pixels are redrawn afterward.

`screensaver_timeout` sets idle seconds (0 disables, the default). The pman test
show uses 3 seconds. Entry fades through black with 4,000 ms per half; waking
uses 200 ms per half and performs the waking button's normal action immediately.
Both transitions wait in black for a complete repaint. Held buttons prevent
entry; presses and releases restart the timer. Startup dismissal and entry-fade
cancellation consume their whole triggering gesture, except main-page C during
idle entry, which cancels the fade and sends immediately. Its successful-send
brightness feedback bypasses remaining fades until release. A+D used for waking never
opens management; release it and press A+D a second time. Setup or asset transfers cancel playback and inhibit
idle entry until they finish; ordinary USB queries do not count as physical input.

USB exposes Serial + MIDI, using the public core's development VID/PID and the
existing `cntrl experimental` product string. The serial receiver accepts show
documents, not HEX or firmware. There is no new application-flash writer and no
factory EEPROM access. The existing PROGRAM recovery path is retained by using
the verified startup and flash configuration. PROGRAM entry and independent
firmware uploads have been exercised, and healthy-application software entry
has repeatedly worked. No intentionally broken-image or interrupted-flash
recovery experiment has been qualified.

The adapter uses the traced native SD interface with the pinned SD/SdFat
libraries. It requires an existing readable FAT/exFAT card. It does not format a
card, migrate vendor data or probe the EEPROM. Named setup storage owns
`/cntrl/setups/NAME/0.bin` and `1.bin`, plus the two selection records under
`/cntrl/selection/`. Old `cntrl-show-0.bin` and `cntrl-show-1.bin` files are left
untouched and are not automatically activated. Copy replaces only the inactive cntrl file, writes a commit
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

Output IDs are `din`, `trs`, `usb-device` and `usb-host`. DIN uses Serial1 and
the dedicated 3.5 mm output uses Serial2, both 31250 baud. The four omniports are
temporarily unused: cntrl does not initialize their pins, read them, or transmit
through them, even when the USB MIDI interface is enabled. Saved stock port
settings are not imported. Switching from stock to cntrl resets the processor;
leaving the ports untouched does not guarantee retention of stock configuration
or a particular voltage or impedance at the jacks.

USB host supports one MIDI device, not a host bridge or hubs.
USB device has two computer inputs and six outputs; explicit pedal USB messages
share input cable 0 with forwarded DIN input. `usb_midi_interface = true` enables
the two physical inputs and the DIN/TRS outputs as a computer interface. The four
configurable USB output cables remain visible but their traffic is discarded.
It defaults off
and does not change enumeration. There is no automatic physical MIDI thru.

The scheduler has independent destination queues, each with 256 forwarding
packets and eight reserved pedal batches of up to 14 bytes. A complete pedal
patch sequence must be admitted before history changes. Device and host USB
submission do not wait for transmit capacity. Congestion causes bounded queuing
and reported drops, not a global stall. See the
[bridge qualification record](../usb-midi-interface-plan.md) for limits and tests.
USB disconnects or later transport failures cannot be acknowledged by a MIDI
receiver and are not equivalent to a confirmed instrument state change.

Display slots follow the physically tested left/centre/right CS order.
The three 160x128 drivers render
through the pinned ST7735_t3 library's RAM framebuffer and font API.
Transfers use blocking hardware SPI at a requested 8 MHz, MSB first, mode 0.
Initialization retains the
traced panel commands, reset sequence and waits. Each runtime call processes at
most one library glyph or one column during ordinary repainting. Unchanged
columns generate no SPI traffic. One 40,960-byte RGB565 canvas is shared across
panels, and a 122,880-byte cache records display content. A second 122,880-byte
snapshot retains the pre-transition panes while destination panes are rendered.
The first pass clears unknown panel RAM.
Rendering snapshots the latest requested pane and coalesces further requests,
including changes received during painting and erasure of shorter text.

All panels are deselected before ending a transfer. Pin 13 is then restored to
GPIO output LOW and pin 12 to GPIO input for the existing switch-register reader.
The next transfer restores the SPI pin mux through `SPI.begin()`. No asynchronous
display transfer or display interrupt masking crosses a button scan. The largest
column transaction sends 267 bytes, approximately 0.27 ms of wire time at 8 MHz,
excluding CPU/peripheral setup. Hardware SPI redraws were user-confirmed as effectively immediate. Live MIDI
overload tests recorded 3 ms maximum button-scan intervals and continued display
servicing, not a visual rendering guarantee. SD operations, particularly the
storage scan, can still stall the main loop. The previous
software-clocked driver had an observed update time around 1,500 ms.

## Capacity measurements

Named-setup document and transfer buffers are placed in RAM2, leaving RAM1 for
code, display buffers and stack. Exact link measurements are printed by
`inspect-patch-switcher`. No external RAM is allocated. Final link
measurements are printed by `inspect-patch-switcher` and may vary as code changes.

The current allocation supports two 65,536-byte document buffers, 32 routed
instruments, 128 categories and 128 Undo entries, plus a 65,536-byte scratch
buffer shared by deletion names and editor-save verification. Record sizes impose an upper
bound that depends on instrument/category tables, banks and version 9's
variable-length name metadata. The advertised patch-count limit is not a
promise that that many fit; the compiler enforces the 65,536-byte limit too. Instrument and
output IDs occupy at most 31 printable ASCII characters; category/patch labels
occupy at most 63 UTF-8 bytes. The compiler fits each line independently; category
and patch names are centered on separate lines using saved cursor offsets,
without runtime width measurement, wrapping or font-size changes. These are the
implemented build's limits, not claims about maximum
hardware capacity. The target JSON records them for the compiler.

Each setup's two slots consume at most 131,272 file bytes, excluding filesystem
overhead. There is no separate document buffer per stored setup. A full
or failing card must leave the old committed slot usable and produce an upload
error; that path is covered with fake storage but not yet physically tested.

## First live upload: 2026-10-22

- Firmware HEX SHA-256:
  `1d350bc7cb018452e835c7fa5f73c0a880ddc5440443023ef8c963eaace9b4bb`.
- The upload-only helper selected the expected HalfKay board and uploaded the
  81 KiB image. The official uploader returned exit status 1 after sending reset
  and reporting that the board disappeared. USB serial nevertheless returned
  `CNTRL READY 1 0 00000000`, confirming the new application booted.
- Uploaded the compiled `examples/patch-switcher/pman.toml` show: 256 patches,
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
Windows display ordered page fields, defaulting to navigation group and patch
name. Partial styles attach to shows, windows, pages, categories, programs,
patches and fields. Each line independently fits its font to the padded width
and allocated height; authors select font names, not sizes. Window padding may
override the show default.
Foreground/background color selection and warnings are resolved on the host.
Rendering fills the whole background and clips all text to the padded rectangle.

Built-in fonts are `classic` (default), `Arial`, and `ComicSansMS`.
Bundled outline families and user-supplied TTF/OTF files use
[sparse SD assets](../bitmap-fonts.md#fonts-supplied-by-a-show), containing only
the glyphs and sizes used by the show. Unicode shaping and missing-glyph checks
run on the host. The pedal draws prepared positions. Classic sizes are 8,
16, ... 96 (6x8 cell scaled by an integer). Native font sizes are 8, 9, 10, 11,
12, 13, 14, 16, 18, 20, 24, 28, 32, 40, 48, 60, 72 and 96; these are the
library's nominal size names. The compiler selects among these available bitmap faces automatically.
Automatic sizing picks the largest supported size fitting each line. The selected size is resolved once on the Mac, never searched on the pedal.

`build_font_metrics.py` exports advances, left bearings, widths and vertical
extents from the pinned library into `patch-switcher-target.json`:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.build_font_metrics
```

Packed glyph metrics in the target file are generated data. The document stores
only selected font IDs/sizes, RGB565 colors, padding, vertical metrics and
precomputed name offsets. Sparse font records reference their SD asset by CRC32;
bitmaps and shaped runs are transferred separately. Classic, Arial, ComicSansMS
and the fixed StillTime cable-test title remain in firmware. Changing show
fonts uses SD uploads without reflashing.
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

## Named performance setups: 2026-10-24

Implemented the portable [multiple-setups plan](../../../../../plan/multiple-setups.md).
The image was built and inspected, then uploaded through software HalfKay entry
and the existing upload-only helper. SHA-256:
`b77c697957e1ebcc6a73b64dad9bbd67ae9933e2c4147801255cf2b112f60ac5`.
The helper independently identified the expected Teensy/HalfKay target. The
updater returned status 1 on reset/disappearance, as previously; the application
then replied `CNTRL READY 7 0 00000000`, establishing boot of the new format.

All 192 Python tests, portable controller/storage/editor tests, target button,
display and renderer tests, and splash tests passed. Native tests use address
and undefined-behavior sanitizers. Ruff, formatting, ty, pyupgrade and diff checks
passed. HEX/ELF bytes and the complete stock flash configuration matched.

Live USB qualification used only two new application-owned temporary setups,
`cntrl-test-a` and `cntrl-test-b`, compiled from the generic example. Each was
662 bytes. Verified:

- Both copies, full byte-for-byte readback and sorted commit-date listing.
- Automatic first selection, forward and backward rotation, and selection query.
- Replacement of the active setup using `--overwrite/-o`.
- Download to TOML and recompilation without definitions/tables, producing
  exactly the original compiled bytes.
- Deletion of the active setup selecting the remaining setup.
- Final deletion returning an empty catalogue and `CNTRL READY 7 0 00000000`.

The temporary setups are logically deleted; their old generations/tombstones
remain recoverable on SD. Qualification output is ignored under
`build/setup-qualification.ULIwVj/`. Manufacturer files, animations and old
single-show slots were not read for migration, modified or removed. No MIDI
messages were requested by these tests. The final state intentionally has no
active setup, following the agreed no-automatic-migration policy.

A+D opens the module menu even without a valid setup; choose Setups for management. A/B browse, C confirms, F
goes back. A+D used to wake the screen is consumed; a second A+D after release
opens management. The numeric editor and this wake gate have native test
coverage. Physical display centering, pedal-operated editing and cold-power
persistence still need user confirmation; no physical interrupted-SD-write
guarantee is claimed.

This link uses 391,164 bytes of program flash. RAM1 has 191,392 bytes of static
variables plus 98,304 bytes of code/padding, leaving 234,592 for the stack.
RAM2 has 214,976 bytes of static allocations, leaving 309,312 for the heap.
The document manager is in RAM2; external RAM remains unused. These are linker
budgets, not measurements of peak runtime stack/heap usage.

### Startup-animation and editor-font fixup

An omitted `splash` now defaults to true, using the installed animation; explicit
false remains authoritative. A no-setup boot also starts the installed animation,
but returning to the empty screen from management or deletion does not replay it.
Existing compiled setups retain their encoded setting until recompiled.

Management text is doubled from classic size 8 to 16. Labels and values occupy
separate screens; long values/names wrap within padding at 12 characters per
line. All 63 characters fit in six lines. Performance text/offsets are unchanged.
Tests cover the default/explicit-false cases, no-setup boot, editor font settings
and padded rendering of a complete 63-character name.

All 194 Python tests and the portable, display, renderer and splash native tests
passed, along with Ruff, formatting, ty, pyupgrade and diff checks. The ARM build
and flash-configuration/HEX checks passed. The inspected image was uploaded via
software bootloader entry and the existing helper, SHA-256
`259673c606ce6214380408afb79e96a35a35f33c8ee1718fc3b73efad7c76606`.
The updater returned its usual status 1 after reset/disappearance; the application
subsequently replied `CNTRL READY 7 0 00000000`. Visible animation and enlarged
editor text await user confirmation. No setup or animation data was uploaded.

## Display pages and per-line styles: 2026-10-26

Compiled format 11 uses named pages with ordered field names, partial styles
and window bindings. The default page contains `group.name` and
`display_name`, preserving the performance UI. Management, empty/error states
and performance pages use the same generic line renderer. See the portable
[display-format design](../../../../../plan/display-format.md) for configuration
and inheritance rules.

The saved pman setup was compared with its previous format 10 binary across all
256 patches in all three windows. MIDI records, labels, font metrics, colors,
padding and centered cursor positions match. The format 11 setup is 34,583 bytes
of the 65,536-byte document capacity. Its default vertical placement and
transition behavior are also covered by native renderer tests. The ARM image
leaves 96,768 bytes of RAM1 for local variables and 309,216 bytes of RAM2 for
heap use; these are linker budgets, not peak runtime measurements.

All 266 Python tests, portable firmware tests and target native tests passed,
including inherited styles, mixed font families, background bands, source
round trips and live numeric field updates. Ruff, formatting, ty, pyupgrade and
diff checks passed. The combined stock/cntrl image passed the builder's stock
patch and boot-envelope checks, SHA-256
`c03c690fecf51f911a411418c62407ec77549cc27ad39b281324fa91b85ebef0`.

The existing upload helper installed the combined image after one USB write
error and a successful retry. After the user's A+F gesture, the converted pman
setup was uploaded, read-back verified and selected. USB reports
`CNTRL READY 11 34583 43363d4a`. The pre-installation pman bytes are backed up
under `build/pman-before-display.bin`; converted source and binary are under
`build/pman-display-v11.toml` and `build/pman-display-v11.bin`.
User confirmation of the visible appearance remains pending.

## MIDI cable-test module

A+D now opens the module menu. A/B choose Setups or MIDI cable test, C opens the
selection, and F returns to performance. On the test page, B toggles transmission
and F stops the test and returns to the module menu. Every entry starts Off.
The center display shows the Still Time title, DIN and TRS result rows, and
On/Off centered above B. The left and right displays are black, except for a
white Back label at the top-right near F. The backlight stays unchanged.
DIN is Serial1 and the dedicated 3.5 mm TRS port is Serial2; Omniports are not used.

The module is included by default. Build `patch-switcher` with `CABLE_TEST=0` to
exclude it and its menu entry. See the generic
[cable-test behavior](../../../../../doc/midi-cable-test.md). Native tests and
included/excluded builds pass. The combined image was flashed through the running
upload helper on 2026-10-27, SHA-256
`4d3a22d26c42b1a861e33a7fb7bc78d84aab0867a443700901011cf7b0fc7ce2`.
The first upload reported a USB write error; the same verified image succeeded
on retry and the pedal enumerated as stock firmware after reset. After the user's
A+F gesture, USB reported `CNTRL READY 14 34597 488beaf6`, unchanged from before
flashing. The selected setup remains pman and all three saved setups are listed.
The three saved setups were downloaded before flashing. The original cable-test
page and loopback behavior were confirmed by the user. The revised centered
layout awaits physical verification.

The centered layout was flashed through upload_helper on 2026-10-27, combined
image SHA-256
`a47039b721fc2537f7356365dd97858b4647384e13a2e11808d38100a3624221`.
After an initial USB write error, the verified retry completed and reset to stock.
After the user's A+F entry, status remained `CNTRL READY 14 34597 488beaf6`
and pman remained selected. Visual verification of the revised layout remains
pending.

## Standalone MIDI interface module

The module menu includes MIDI interface when `MIDI_INTERFACE=1` (the build
default). It starts Off on every visit. B toggles the existing DIN/TRS USB MIDI
bridge; F returns to the menu and restores the setup's previous bridge setting.
The setup is not edited or saved by the module.

The center screen shows host USB configuration state, DIN/TRS input and output
activity indicators, and On/Off above B. The outer screens are black with Back
near F; brightness is unchanged. Each indicator remains lit for 100 ms after a
complete input packet or successful physical output submission. USB configured
status does not indicate that a DAW has opened a port.

Build with `MIDI_INTERFACE=0` to omit the module and its menu entry. Cable tester
inclusion remains controlled separately by `CABLE_TEST`. The module has no
runtime dependency on the tester. The generic implementation and verification
contract is in [the module plan](../../../../../plan/midi-interface.md).
This module has not been flashed; physical forwarding and UI checks remain
pending.

## Setup revisions

Revision records live at `/cntrl/history/SETUP/REVISION/0.bin`, with revision
numbers encoded as 16 hexadecimal digits. The record contains the label,
timestamp, complete setup, checksums and completion marker. Deleted record files
leave their directories to reserve numbers. These directories are separate from
`/cntrl/setups`, so history does not enter the performance setup catalogue.
See [revision history](../../../../../doc/setup-revision-history.md) for commands
and pedal controls. This implementation was built and tested without flashing.

The cooperative history follow-up was flashed with the running upload helper on
2026-10-27. Combined image SHA-256:
`2c564a6b7268116238e8ef1fc9369cbaf864a290034ba5cc92c9d99d05a01aab`.
The first upload reported a USB write error; the identical verified image
succeeded on retry, resetting to stock firmware. Before flashing, pman was
selected and USB reported `CNTRL READY 14 34597 488beaf6`; its source was backed
up. Post-A+F verification and physical history latency measurements remain pending.
History I/O now yields between blocks and directory entries. The native budget
test uses a 64 KiB document and over 2,000 revision entries. Individual SD calls
still require physical latency qualification.

Post-A+F verification confirmed the unchanged pman selection and checksum but
exposed `invalid-command` for HISTORY LIST. The target nano C library does not
support the long-long scanf conversion used for revision numbers. Revision wire
numbers now use explicit bounded decimal parsing/formatting, and directory names
use two 32-bit hexadecimal halves. Native boundary and protocol tests pass.
The corrected combined image was flashed through upload_helper, SHA-256
`02766ccbb3922bbf623862a267c9aae233475c7377eae8672b8cffc543f28360`.
Upload succeeded after the initial USB write error and retry. The pedal reset to
stock firmware; the corrected live history-command check awaits A+F entry.

On 2026-10-28, after A+F entry, the corrected firmware reported
`CNTRL READY 14 34597 488beaf6` with pman still selected. The host history-list
command completed successfully with no revisions listed, confirming that the
revision-number parser accepts the live request. This was a read-only check;
physical snapshot/restore behavior and MIDI latency remain unqualified.

## C-button program-change brightness feedback

On the performance page, C boosts the shared display backlight to maximum after
an accepted send changes that instrument's last-sent program/bank. It stays at
maximum while C is held and returns to the setup's brightness on release. There
is no minimum flash duration. Repeated selections and name/volume-only patch
differences do not boost brightness; rejected sends do not boost it either.
The first accepted send counts as a change because previous state is unknown.
Cancelling gestures, entering the module menu, deactivation and setup activation
also clear the temporary boost.

Portable tests cover hold/release, repeated selections, failed sends, duplicate
press edges, undo, cancellation, setup activation/deactivation, same-program patch
aliases, and brightness values 0 and 255. Native adapter tests and the combined
firmware build pass. The combined image was flashed with upload_helper on
2026-10-28, SHA-256
`7d71d2f403cdb0517775d6c8b01dbd27ac6633a42839818ac2b5bbef3176c1ab`.
An initial USB write error was followed by a successful retry of the same image.
USB returned as stock firmware. Before upload, pman was selected with
`CNTRL READY 14 34597 488beaf6`. After A+F, USB reported the same status and pman
remained selected. Visible brightness-feedback verification remains pending.

### Idle-fade interaction correction

The user's first-press report exposed an interaction with pman's three-second
idle timeout: screensaver entry cancelled and consumed the first gesture, while
temporary brightness still passed through fade scaling. Main-page C now cancels
idle entry and performs its normal send. Temporary brightness is an explicit
hardware override, bypasses fades, and updates PWM at the press/release edges;
a successful change stops remaining idle/wake fading. Startup dismissal still
consumes its gesture, other buttons retain their existing entry policy, and no
minimum flash duration is added.

Portable, adapter and splash/runtime tests pass. Regression coverage includes C
during UI fade-out, animation painting and animation fade-in, short release and
startup-dismissal isolation. The splash harness was updated to the current Pane
and document-version interfaces so this interaction is exercised again.
Combined image SHA-256:
`7deaa29ee65217f0d4237dda0f2d9096984fc4e9ada781361bb553601daedee5`.
Flashed through upload_helper on 2026-10-28 after one initial USB write error and
a successful retry. Stock USB returned after reset. The pre-flash setup status
was unchanged at `CNTRL READY 14 34597 488beaf6`. After A+F entry, the user confirmed
that both held and quick presses work in the requested reproduction test. USB
status remains `CNTRL READY 14 34597 488beaf6`, with pman still selected.

## Patch-mode default and Original firmware module

Fresh combined images boot directly into patch mode. A+D opens the module menu;
select Original firmware, then C to commit the stock boot choice and reset into
the manufacturer firmware. F leaves without switching. Stock retains its A+F
50 ms/no-hold gesture to persist patch mode and reset back. Explicit firmware
choices still persist across power cycles. The UI action is independent of setup
selection, does not send MIDI and leaves SD data unchanged.

The module uses the audited selection sector and append-only record format. It
verifies the stock record before reset and preserves a slot for A+F's return.
It can be excluded with `ORIGINAL_FIRMWARE=0`. Native tests cover fresh default,
torn selections, explicit choices, journal capacity and the module's explicit
start/failure/back behavior. Physical round-trip verification is pending.

Flashed through upload_helper on 2026-10-28. USB enumerated directly as cntrl
without an A+F press, confirming the fresh patch-mode default. Status remains
`CNTRL READY 14 34597 488beaf6`, with pman selected. Combined image SHA-256:
`eeaf97a0300a610cfd9f1372420b4226d89d81f25effb99bffdc7caf9bb9b49c`.

## Panic module

Panic is an optional module, enabled by default; `PANIC=0` excludes its code and
menu entry. B starts Panic and holds maximum backlight brightness using the
existing temporary feedback path. F cancels and goes Back. The center screen
contains only Panic above B; the outer screens are black except for Back near F.

Outputs are DIN, dedicated TRS, USB device cable 0 and the connected USB MIDI
host device. Disconnected USB outputs are skipped. Omniports remain untouched.
The module pauses the normal bridge while it owns the ports and restores the
setup setting on exit. It does not edit the active setup or songs/setlists.
See the [Panic behavior and test contract](../../../../../doc/panic.md).

Panic was built and flashed on 2026-10-28 from the rebased main checkout,
preserving the songs/setlists and Original firmware modules. Portable/native
tests and PANIC-enabled/disabled builds pass. Combined image SHA-256:
`a1a639f3bff6661770e82ff10bf49c0de2e1dc89991453b87eea11f1a86aa07f`.
The running upload helper reported a USB write error on the first attempt. The
same verified image reached reset on retry; the updater returned a disappearance
error, then USB enumerated as cntrl. Post-flash status was
`CNTRL READY 14 34597 488beaf6` and pman remained selected. No Panic messages were
triggered by automation. Physical hold feedback and MIDI capture remain pending.
