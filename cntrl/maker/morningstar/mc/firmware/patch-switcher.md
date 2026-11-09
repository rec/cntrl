# MC6 Pro patch-switcher build

The application is built and running on the pedal. It is not a fully
hardware-qualified release. Dated sections below retain earlier image hashes,
format numbers and measurements; they are historical, not the current build.

## Setup residency and saves

Current movie configuration uses `splash_movie` and `idle_movie` filename lists,
with automatic preparation and upload alongside setups. Empty lists disable that
category. Movies stream from SD and keep their pixels out of RAM2; references
are included in compiled setup sizes. See the
[movie workflow](../splash/README.md). Document format is now 34. Historical
single-asset and `splash` boolean descriptions below are superseded.

Runtime setups come from one bundle in `/cntrl/setups/.bundle/0.bin` and `1.bin`.
The build includes requested setups and their companions in a sorted index table.
A fitting collection stays resident in RAM2; otherwise the bounded LRU is
prefilled to maximize resident bytes. Page RAM declarations and the configurable
16192-byte memory margin guide the initial prefill plan only. Actual setup,
document and font allocations share the linked heap budget. The other cntrl RAM2 reserve
is currently an explicit 288 KiB estimate, not a measured peak.

On-pedal saves rewrite the inactive bundle copy. CRC validation and the completion
marker preserve the previous bootable copy after an interrupted write. History
remains per setup and includes the index-to-name context needed for a later table
reordering. Setup selection uses resident bytes on a cache hit; fonts and other
assets retain their existing storage paths. Whole-bundle validation and saves
perform SD I/O; physical timing during those operations remains unqualified.

## Preparing an upload image

After building and inspecting the combined firmware, use its absolute path and
exact SHA-256 with the existing upload helper:

```bash
python -m cntrl.maker.morningstar.mc.upload_helper upload --image /absolute/path/to/firmware.hex --sha256 DIGEST
```

The helper snapshots and validates the requested worktree image before flashing
the expected pedal. No staging copy in another checkout is needed. Start the
helper manually and enter the bootloader first, following the
[upload helper guide](upload-helper.md).

## Switch response

The setup's root `fast_switches` boolean defaults to `true` and applies to all
six buttons. Each press or release triggers on its first sampled edge once the
previous transition has settled. Debounce suppresses subsequent bounce on both
edges, including phantom press/release pairs after release. If an opposite
state persists during settling, it is accepted after the debounce interval.
With `fast_switches = false`, both edges wait for stable input before triggering.

Switch assignments with a `gestures` table use its configured `debounce`
duration (20 ms by default); other buttons retain 15 ms. The recognizer adds
only the waits required by configured double taps, long presses and repeats.
Switch scans are due every 1 ms. A due scan takes priority when an active
display DMA transfer finishes, before another queued band starts. The renderer
also yields before starting work when a scan is due. Active transfers finish
normally; shared-pin occupancy and other work can extend the actual scan gap.
Fast mode accepts an isolated first-edge
glitch as a real transition; debounce cannot retract an action already sent.

The compiled setup format is version 33. Recompile setups with the matching
compiler before deploying this firmware.

Accepted button actions and MIDI output service run before serial transfers and
background SD work. A new switch invocation starts dispatching in the same
service pass, and buffered USB MIDI is flushed before MIDI-file SD work.
These ordering changes have host tests and build checks; latency still needs
measurement on the pedal.

## Browsing transitions

By default, moving to the next cycle slides the candidate pane rightward; moving to the
previous cycle slides it leftward. Browsing patches within a cycle scrolls
rightward, as does sending a patch. The setup's `[scrolling]` table independently
selects `send`, `patches`, and `cycles` directions (`left`, `right`, `up`, `down`).
Backward browsing and undo reverse those directions. Vertical sends animate the
candidate and current panes together while the page window stays still.
Leftward sends slide each performance pane's new content into place; the default
rightward send retains the continuous panorama movement.
The transition-start routine resides in flash to keep the combined application
within the target's RAM allocation; the animation loop remains in fast RAM.
The vertical motion uses software redraws
in the existing display orientation. Two alternating DMA buffers each hold at most
eight columns (2 KiB of pixel data). The renderer prepares one band while SPI
transmits the other. Completion releases the shared SPI/button pins before the
next button scan and transfer. Every frame moves thirty-two pixels through four
positions. Only the changed span of each band is transmitted, and all positions are displayed even when
redrawing runs behind. `scrolling.horizontal_duration` defaults to 75 ms and
`scrolling.vertical_duration` to 200 ms. Both accept seconds or duration strings
such as `'200 ms'`. Actual duration can exceed
the requested duration because a complete frame must finish before the next.
The current-patch pane and page window stay in place during this motion.

## Scrolling investigation

The [MC6 Pro scrolling investigation](../scrolling-scan-timing.md) is the single
reference for analysis, current diagnostic instructions, deployment provenance
and historical scrolling experiments. The [plan](../../../../../plan/scrolling-bug.md)
remains separate.

Temporary scrolling-debug code is excluded from normal builds. Explicit
`DIAGNOSTICS=1` defines `CNTRL_DIAGNOSTICS` to include it. User-facing Cable Test,
storage inspection and MIDI status commands retain their existing behaviour.

## Historical deployment, 2026-10-25

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

## Sliding send/undo panes

The performance panes are candidate (left), current (centre), history (right).
Pane indices are physical left-to-right throughout: 0 candidate, 1 current,
2 history. Setup format 21 stores window graphics and page references in that
same next/sent/undo order. The hardware adapter forwards pane indices directly,
as it does for module screens. Recompile setups before uploading to format 21.
A sends a patch: candidate moves right into current, and current into history.
D undoes along the reverse path. B browses patches and E browses cycles, with
hold-to-repeat and D-held reverse as described below. C taps tempo on the Tap Tempo page; F cycles the rightmost page.

The centre pane retains the current patch after Undo. An instrument's first send
is an undo barrier because its previous patch is unknown. Undo stops there without
changing the candidate, current patch, or other instruments; sending another patch
on that instrument makes that later send undoable.

When Undo can restore a patch, the candidate pane shows a circular undo light in
the top-left corner of its title bar. It has two pixels of padding and is drawn
behind the title text. Its colour is the first of green, blue, black, and white
with at least 3:1 luminance contrast against the title background. The light is
off at an undo barrier and while the controller is executing a patch.
Pages keep their existing controls and displayed instructions.
Animation does not delay MIDI/history actions.
For scrolling behaviour, evidence, deployment hashes and limitations, see the
[scrolling investigation](../scrolling-scan-timing.md) and its
[original transition history](../scrolling-scan-timing.md#original-patch-switcher-transition-history).

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
response is `CNTRL STORAGE CAPACITY_BYTES USABLE_BYTES FREE_BYTES ALLOCATION_UNIT_BYTES`.
FREE_BYTES is `-` when not requested. `SCAN` first emits optional
`CNTRL DETAIL KEY_HEX VALUE_HEX` rows. This driver reports card type, manufacturer,
OEM, product, revision, serial number, manufacture date, filesystem and raw CID;
it interprets the identification register and FAT type locally. Details are UTF-8
strings encoded as hex and do not affect entry numbering. The shared protocol
also supports storage without a card or FAT filesystem. `SCAN` then streams
`CNTRL ENTRY ID PARENT D_OR_F SIZE_HEX NAME_HEX` for each entry and ends with
`CNTRL STORAGE END COUNT`. Root has ID 0; entries have consecutive IDs starting
at 1, with parents preceding children. Sizes use 16 hex digits, preserving
64-bit exFAT sizes; hex-encoded names preserve spaces.
The host rejects partial reports and reconstructs paths and totals on the Mac.
The short `CNTRL STORAGE` query returns byte counts without scanning. Errors use
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
foreground-color stippling for the candidate patch name when it matches the last
successful send for its instrument (program and bank select, not label/category).
It also retains instrument identities, logical banks and bank mappings for
self-contained editable downloads. Version 9 preserves original synthesizer
names and the separate program/show `display_name` overrides in variable-length
metadata; rendering uses the resolved label. Downloads embed the used program
descriptions in `definitions.programs`. The default-off per-setup
`is_midi_interface` switch remains available. See the [USB MIDI interface plan and qualification
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
cancellation consume their whole triggering gesture, except main-page A during
idle entry, which cancels the fade and sends immediately. Its successful-send
brightness feedback bypasses remaining fades until release. C+F used for waking never
opens management; release it and press C+F a second time. Setup or asset transfers cancel playback and inhibit
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

DIN uses Serial1 and the dedicated 3.5 mm TRS output uses Serial2, both at
31,250 baud. USB host supports one MIDI device. USB device exposes two inputs
and six outputs. With USB MIDI forwarding enabled, USB output cables 0 and 1
send to DIN and dedicated TRS. Cables 2 through 5 send to configurable jacks
1 through 4 when their active setup selects a MIDI mode.

### TRS Ports page

From the performance screen, press F to reach **TRS Ports** after Panic.
A selects a jack, B previews its next mode, and C saves it with the selected
setup. The first row shows the active mode. Switching setups restores all four
roles. An unused jack defaults to Off. If the setup routes an instrument or
patch to a jack, change that route before switching the jack to an input.

```toml
[trs_ports]
configurable-1 = 'midi-a'
configurable-2 = 'expression'
configurable-3 = 'aux'
configurable-4 = 'midi-b'
```

Use the same jack name as a MIDI patch or instrument's `output`. The
`CNTRL MIDI STATUS` counters use routes 2 through 5 in jack order.
Type A sends data on tip with ring high. Type B swaps tip and ring.
Expression uses tip as analog input and drives ring high. Aux uses tip and ring
as switch inputs pulled up to detect contacts closing to sleeve. Off releases
both conductors. Expression-to-CC and external switch assignments are separate
features. Physical loopback qualification is pending for both Type A and Type B.

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

Named-setup document and transfer bytes are allocated from RAM2's heap, leaving RAM1 for
code, display buffers and stack. Exact link measurements are printed by
`inspect-patch-switcher`. No external RAM is allocated. Final link
measurements are printed by `inspect-patch-switcher` and may vary as code changes.

Setups and fonts share one allocation budget. The linker derives it as
`_heap_end - _heap_start`, the RAM2 heap remaining after this firmware's static
allocations. There is no manually configured document or font byte ceiling.
Active setups, cached setup images, staging, scratch, embedded song
documents and loaded font data all count towards the same budget. Each buffer
is sized to its contents; staging and scratch are released when idle. Reusing an
active font retains its allocation and counts it once. During a setup change,
both old and newly loaded data count until the change commits or is cancelled.
Allocation failure preserves the active setup and fonts and reports an error.
Other runtime allocations and allocator overhead also consume the physical heap,
so an allocation may fail before the tracked payload bytes reach the linked limit.

The target supports 32 routed instruments, 128 categories and 128 Undo entries.
Record sizes impose an upper bound that depends on instrument/category tables,
banks and variable-length metadata. The advertised patch-count limit is not a
promise that that many fit. Memory fit is enforced on the pedal using the budget
from its firmware build. Instrument and
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
The library's static initialization reserves three inactive DMA channels. Vertical
browsing transitions now use the SPI library's asynchronous DMA transfer API;
the graphics library still does not flush its framebuffer. Our driver owns panel initialization,
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
and the fixed StillTime tool titles remain in firmware. Changing show
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

Implemented named setups as described in the
[application guide](../../../../../firmware/README.md).
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

C+F opens the module menu even without a valid setup; choose Setups for management. A/B browse, C confirms, F
goes back. C+F used to wake the screen is consumed; a second C+F after release
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
[display formatting guide](../../../../../doc/formatting.md) for configuration
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

## MIDI cable-test page

MIDI Cable Test follows Firmware, with Panic after it. C starts or stops
transmission; F stops any active test and continues the cycle. Every entry starts
Off. The shared page style supplies the background, heading and text weight.
DIN and TRS occupy the matrix rows, with Out then In columns and a bottom-right
toggle. DIN test data is blue and TRS test data is green; each input shows the
color of its latest matching test stream.
The other two displays remain in place. DIN is Serial1 and the dedicated 3.5 mm
TRS port is Serial2; the cable tester exercises only these two routes.

The tester is included by default. Build `patch-switcher` with `CABLE_TEST=0` to
exclude it and its page. See the generic
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

## Panic page and USB MIDI forwarding

Panic follows the cable tester in the page cycle.

On Panic, C starts the controller reset pass and holding C continues Note Off
sweeps. Releasing C restores brightness; F cancels sending and advances. Normal
bridging resumes when sending completes or the page is left. Build with
`PANIC=0` to omit this page.

USB MIDI forwarding is controlled by the setup and patch state operations,
without a separate page. The cable tester now owns the Out/In matrix;
its source colors identify test messages, not general MIDI activity. See
[the interface guide](../../../../../doc/midi-interface.md) and
[the Panic guide](../../../../../doc/panic.md).

This section records the original build-only verification. The module was later
included in combined firmware uploads; dedicated physical forwarding and UI
qualification remains pending.

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

## Send-button program-change brightness feedback

On the performance page, A boosts the shared display backlight to maximum after
an accepted send changes that instrument's last-sent program/bank. It stays at
maximum while A is held and returns to the setup's brightness on release. There
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
host device. Panic also sends to configurable jacks assigned a MIDI output role. Disconnected USB outputs are skipped.
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


## Pending-patch pixel pulse

While the leftmost performance pane contains an unsent patch, only its pixel
colors pulse from full intensity (128) to 50% intensity (64) and back over 4000 ms. Each 2000 ms
half-cycle uses cubic smoothstep (`3t² - 2t³`), with zero slope at both ends.
The implementation uses bounded 32-bit fixed-point arithmetic and no sine calls,
allocation, delays or additional timers.

The pulse stays at 128 for the configured delay (2 seconds by default) before starting, and restarts its delay
when navigating to a different patch or cycle. Repainting the same patch does
not restart it. Already-sent patches,
empty panes and other module pages use the configured brightness. Temporary
hold feedback takes priority, and splash/screensaver playback retains its own
brightness and fades. When the pulse resumes after an override it starts at 128.
The shared backlight stays at the configured brightness. Pixel scaling happens
only when transmitting the left panel's RGB565 columns; cached pixels remain
unchanged. Idle polls refresh at most one 128-pixel column, and content repainting
and scroll animations take priority. No extra framebuffer is allocated.
The pulse never changes the saved brightness setting.

## Patch and cycle navigation animation

Changing patch cycles or browsing patches within a cycle slides the new card
into the physical left target window:
from the left when navigating forward, and from the right when navigating
backward. The center and right windows stay fixed. This uses the setup's
transition duration and the existing two-column hardware scroll scheduling.
Each panel retains its own scroll offset so later send and undo animations
continue to move all three windows together correctly.

## Patch-manager vertical bands

Patch-manager cards use three fixed vertical bands covering the full 128-pixel
height: rows 0–41, 42–84 and 85–127. Each line's font box is centered in its own
band. Empty lines leave their band empty rather than redistributing the other
lines. The reversed heading fills the top band; the second and third lines keep
their shared fitting font size. Other module pages retain their existing layout.

## Adjustable presentation values (setup format 22)

These settings can be changed in the setup TOML, then recompiled and uploaded:

```toml
# Ceiling on weighted RGB brightness for generated cycle foregrounds.
foreground_brightness_limit = '50%'
# Existing settings: brightness = 192, transition = '500 ms'.
# Omitted transition uses the renderer's 75 ms fallback.

[target_pulse]
delay = 2  # Seconds at full brightness before the throb; zero disables the delay.
period = 4  # Seconds for a full bright-to-dim-to-bright cycle; 0.002..65.535.
minimum = 64
maximum = 128
```

All configuration durations use seconds for bare numbers. Explicit units such
as `period = '4000 ms'` are also accepted through `reccy.configuration.units`.
`transition`, navigation timings and the build option `switch_window` follow the same rule.
Durations must resolve to whole milliseconds for the firmware's timer precision.
The `_ms` configuration names have been removed; milliseconds remain an internal
wire-format and firmware unit.

Pulse intensities range from 0 to 128, where 128 leaves the pixel colors intact.
The minimum must not exceed the maximum. Equal limits make the intensity steady.
The model supplies the defaults once; the compiled setup carries explicit pulse
settings, and the firmware reads them without supplying another default. Setup
format 22 requires recompiling and replacing older compiled setups.

The page `background_color`, `seed`, `fill_in_styles`, `animation_color_cycle`,
`brightness`, `screensaver_timeout` and `transition` are existing
setup settings. `horizontal_scale` belongs to styles; StillTime's font metadata
sets its default to 1.5. The fixed three-band patch-manager structure and the
128-level pixel intensity scale are layout and encoding definitions.

Stock firmware cannot read the setup's settings. Its exit-gesture window is a
build option, defaulting to 50 ms:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.boot_switch --switch-window 0.05
```

This window accepts 1..112 ms, limited by the stock hook's 128 ms input history
and its 15 ms debounce. Input delay is derived from the chosen window plus the
debounce interval. It still requires no hold. Changing it requires rebuilding
and flashing the combined image. Other setup settings only require recompiling
and uploading the setup.

## Four-button patch-manager navigation (setup format 23)

Physical buttons A/B/D/E keep their primary actions: A sends the target, B browses
patches forward, D undoes the latest send, and E browses cycles forward. C taps tempo on the Tap Tempo page; F cycles Tap Tempo, Status, Firmware, MIDI Cable Test and Panic, starting on Status. C on Firmware opens a confirmation page; release C, then press F to switch to stock or C to cancel; A+F returns to cntrl. Old C/F browsing and the C+F module-menu shortcut
are removed. Other modules keep their existing button assignments.

Status includes a persistent **Starts** count in its final row. It increments
once whenever cntrl starts, including restarts when returning from stock
firmware. The first successful save on a new card records 1. Opening Status,
changing setups and browsing pages do not increment it.

The counter belongs to the card and is stored in `/cntrl/starts.toml`, separately
from setups and songs. It alternates two checked records in separate 512-byte
sectors and verifies each update after syncing it. An interrupted update can
recover from the previous valid record. A nonempty file without a valid record
is reported as an error rather than silently resetting the count. `Starts: --` means no
card was mounted. Failures show the operation and slot, for example
`Starts: sync s1` or `Starts: decode s0`. Short reads and writes include the
actual byte count out of 512; a read-back mismatch includes the first differing
byte offset, for example `Starts: mismatch s0 @37`. The diagnostic is retained
for that boot. If neither existing record is usable, the first failure is shown;
recovering from one valid record and saving successfully clears the diagnostic.
`cntrl status --port /dev/cu.usbmodem144682801` also reports these values over USB,
appending `starts`, `starts_status` and, on failure, `starts_error` to the
`CNTRL READY` reply. The count is the last confirmed value, so it can remain zero
when writing or verifying the first update fails.
Counts are converted to decimal text without 64-bit `printf` conversions, which
the pedal's C library does not support. The checksum is written as two eight-digit
32-bit hex words. This preserves the existing record format. Previously malformed
records containing `lu` or `lx` remain invalid and are not silently reset.
To deliberately restart the count at 1, run `cntrl status --reset-starts --port
/dev/cu.usbmodem144682801`. This sends `CNTRL STARTS RESET`, truncates only the
counter file and uses the normal write/sync/read-back verification to save 1.
Reset is rejected during MIDI recording. The next firmware start records 2.

Live verification on 2026-11-07: reset reported `starts=1 starts_status=ready`;
a second status read still reported 1. Entering HalfKay and resetting the existing
application then reported `starts=2 starts_status=ready`. The loaded setup stayed
at format 34, 23,084 bytes, checksum `a4c4967e` across that restart.

Raw counter bytes can be inspected over the same USB command transport without
changing the file. `CNTRL STARTS SIZE` returns `CNTRL STARTS SIZE <bytes>`.
`CNTRL STARTS READ <offset>` returns `CNTRL DATA <hex>` containing up to 128 bytes
from that zero-based file offset. Read successive chunks through the reported
size to preserve both 512-byte records, including line endings and padding. An
offset at or beyond EOF is rejected. These commands open only `/cntrl/starts.toml`
read-only; they do not create, increment, repair or truncate it.
Startup continues in either case. The counter uses card storage exclusively.

The pages share one heading font and size, with white
text on a black heading band. All titles fit against the same reference text.
Status labels are dark blue and values are green. Page titles and small text
default to Atkinson Hyperlegible Next SemiBold, rasterized from the outline at
individual pixel sizes. Its weight comes from the font itself. The page background and Tap Tempo ring thickness
are configured in the setup:

```toml
[page_settings]
title_font = 'AtkinsonNextSemiBold'
status_font = 'AtkinsonNextSemiBold'
background_color = '0xFFF085'
annulus = 5
off_color = '0x600000'
flash_fraction = '40%'
```

The flash occupies the configured fraction of each measured tap cycle. Holding C
also lights the indicator, even before a tempo is established; releasing C
returns to the normal beat flash. Setup format 26 stores the fraction in thousandths. Recompile
saved setups after replacing `flash_duration` with `flash_fraction`.

These are the defaults. Colors use the existing `0xRRGGBB` syntax and are
quantized to the display's RGB565 format.

B and E step once immediately. Holding either begins repeating after the hold
delay and then at the repeat interval:

```toml
[navigation]
hold_delay = 1.5
repeat_interval = 1
```

Bare values are seconds; explicit units such as `'1500 ms'` work too. Both
settings accept 0.001..65.535 seconds in whole-millisecond increments. The first
repeat occurs at the hold delay, not one interval later. Delayed polls skip
missed repeats instead of catching up in a burst.

While B or E is down, pressing D immediately steps backward in that collection
and consumes D's undo action. Reverse applies to subsequent B/E presses and
holds only until D is released. Changing direction restarts the hold delay;
releasing B/E stops that button's repeats. D alone still undoes, and A retains
its existing send and hold-brightness feedback. Setup changes and leaving the
page cancel held-button state. The displays and scrolling animations are unchanged.

While any physical button is held, including C/F, the target window
stays at full pixel brightness. The pulse starts from full brightness only after
all buttons are released and the configured delay has elapsed. This also suppresses it throughout hold delays and
auto-repeat in either direction.

The old short chord-delay setting is removed. `navigation.hold_delay` replaces
its header slot in format 23; the repeat interval is stored in the metadata
header. Recompile setups before uploading them to this firmware.

The auxiliary pages and all their configuration defaults are documented in
[the firmware guide](../../../../../firmware/README.md#auxiliary-patch-manager-pages-format-24).
