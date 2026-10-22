# MC6 Pro USB/button/display experimental firmware

The first upload has booted and communicated over USB serial on the pedal.
For subsequent uploads, see the [upload-only helper](upload-helper.md), whose
live upload and subsequent USB heartbeat have been verified on this pedal.
It is an independent application using the public Teensy core, not a patched
Morningstar executable. It is not yet a functional MIDI controller.

## Build

On macOS Apple Silicon, with `make`, `curl`, `tar`, `patch` and `shasum`:

```bash
make -C cntrl/maker/morningstar/mc/firmware tools
make -C cntrl/maker/morningstar/mc/firmware test
make -C cntrl/maker/morningstar/mc/firmware inspect
```

Run these from the repository root. Tool paths are resolved relative to the
Makefile. The first command downloads Arduino CLI 1.5.1, verifies its pinned
SHA-256, and installs PJRC's `teensy:avr@1.62.0` package and its dependencies,
including Arm GCC 15.2.1. Everything stays in this directory's `.tools/`; Python
dependencies and global Arduino installations are unchanged. The package
installer verifies archive checksums from the official indexes.

The target is `teensy:avr:teensy41`, USB Serial, 600 MHz, smallest-code
optimization without LTO. This does not establish that the MC6 Pro is a complete
Teensy board or that every default matches its hardware.

`build` compiles only. Both the Teensy Loader post-build hook and the optional
encryption hook are replaced with `/usr/bin/true`. There is no upload target,
port selection, serial monitor, or device command. A build must not hand its
image to an already-running automatic loader.

`inspect` rebuilds, checks that decoding the HEX reproduces the ELF-derived raw
image, verifies the complete 512-byte flash configuration against its stock
SHA-256, and reports hashes, ELF headers, load segments and section sizes. Ignored
outputs are under this directory's `build/`:

- `usb_probe.ino.hex`: upload-format image, not permission to upload it.
- `usb_probe.ino.elf`: linked executable with debug information.
- `usb_probe.ino.bin`: raw flash contents.
- `symbols.txt` and `disassembly.txt`: inspection evidence.

## Application and startup boundary

The product string is `cntrl experimental`, manufacturer `cntrl`. USB Serial
uses the core's VID:PID `16c0:0483`, not Morningstar's ID. This is a private
development identity, not an allocation for a shipping product. With a serial
connection open and DTR asserted, the diagnostic application emits a line such
as `cntrl raw ms=1200 scans=400 bits[7:0]=11111111` every 100 ms. Bits are
printed most-significant first, including the two unknown inputs. Uptime and
scan count distinguish fresh samples from buffered output or a stalled scan.
No serial commands are parsed. These lines replace the original heartbeat.
Do not monitor at baud 134: the core interprets it as a bootloader request.

The application scans the six footswitches every 3 ms through pins 28 (load),
29 (gate), 12 (input) and 13 (clock). It prints `button A pressed` and
`button A released`, similarly through F, after 15 ms of unchanged samples.
Buttons held at boot are reported after debounce if serial is connected.
Events occurring while disconnected are not queued or replayed.

The button-event image also reports `held` once after 500 ms from a debounced
press, and `double pressed` on the second debounced press within 300 ms of a
short press's release. The 300 ms boundary is inclusive. Press/release events
remain immediate after debounce; these extra events do not replace them.
A hold does not arm a double press, and double presses use non-overlapping
pairs. The second press of a double can itself produce a hold if kept down.
Each event includes device uptime in milliseconds. Buttons are independent;
simultaneous presses produce separate events, not a new chord action.

Display chip-select pins 15, 24 and 27 are held HIGH before enabling the
shared clock. Output latches are preloaded with constant-pin `digitalWriteFast`
before `pinMode`; the pinned core's ordinary `digitalWrite` would only change
pull resistors on an input.
The scan follows stock reader `0x2648`, with 3 us load-low, load-high and
clock-high delays. An explicit 3 us clock-low delay accommodates inlined GPIO.
Bits 2, 1, 0, 3, 4 and 5 map to A through F, active low; bits 6 and 7 are ignored.

There are no MIDI, EEPROM, SD-card, flash-programming, relay or configurable-port
operations, and no presets. There is no LED blink: pin 13 is a shared clock.

At startup, `usb_probe/displays.cpp` initializes all three panels and draws
white `CNTRL` and `DISPLAY 1`, `DISPLAY 2` or `DISPLAY 3` on black. These numbers
identify firmware slots, not yet established physical positions. This fixed-text
bring-up uses independently drawn 5x7 glyphs only for the greeting, not a complete
font or a runtime text API. There is no framebuffer, display library dependency,
serial text command or vendor executable code.

The driver uses MOSI 11, shared clock 13, D/C 34/23/25, reset 21/22/20 and
the existing CS pins. Panel power/gamma values are traced hardware data.
Hardware reset follows stock's HIGH/LOW/HIGH waits of 100/100/200 ms. Software
reset and sleep-out waits are lengthened to 150 and 500 ms, matching the public
[Adafruit ST7735 driver](https://github.com/adafruit/Adafruit-ST7735-Library/blob/master/Adafruit_ST7735.cpp).
Rotation 1 uses MADCTL `0xa8`, 160x128 pixels and x/y offsets 1/2.
Pin 6 PWM starts at stock's value 5, then changes to 63 after drawing, equivalent
to stock brightness setting 1 without writing that setting to storage.

Drawing happens only in setup and takes several seconds. Button scanning starts
afterward. The displays are deselected and clock LOW on return, so later button
scans cannot send display data. Runtime redraws interleaved with scanning have
not been implemented or validated.

`no-external-ram.patch` removes the startup call to `configure_external_ram()`,
which would otherwise drive optional memory pins. It patches only the local
core. Every build checks that it remains applied. The upstream function body
remains linked, but inspection found no call to it. Setup accepts the original
or already-patched core; an incompatible patch fails the build.

`external-flash-config.patch` lets the sketch supply its own flash configuration
instead of the generic core default. `usb_probe/flash_config.c` describes the
known-good MC6 Pro settings as hardware data. Both patches are checked in the
explicit reverse direction with automatic direction guessing disabled.

This is not a no-hardware-access image. The core configures clocks, caches,
RAM, timers, RTC, temperature monitoring, internal ADC/PWM blocks and USB1.
The ADC/PWM startup routines do not configure external pin muxes. Boot reads
external application flash. Application GPIO access is limited to the
switch-reader, display and backlight pins listed above.

The linked USB ISR retains the CDC baud-134 request and timer leading to
`_reboot_Teensyduino_()`. That routine reads security state and uses the normal
breakpoint or secure-ROM entry path; it does not program fuses. This is static
evidence, not a successful test on this board. PROGRAM remains the established
physical recovery method.

## Button image inspection, 2026-10-21

`make -C cntrl/maker/morningstar/mc/firmware test inspect` passed without compiler
warnings. Host tests
cover debounce, repeated samples, release, timer wraparound and A-F bit
selection. They do not establish electrical behavior on the pedal.

- Raw image: 21,504 bytes; HEX SHA-256:
  `8b60338ce17700d0239a6d3dd0218b3fb85d4da7533e3b272534396b6007f5b9`.
- Raw SHA-256:
  `ca6a40df8dd24a02ba8c4ff875944ff0a3bf6727321f9e7b4404f4bcfef5d808`.
- Boot data length `0x5400`, Thumb reset `0x600015e5`, zero CSF at `0x60004800`.
- Stock 512-byte flash configuration verified; ELF-derived and HEX-derived
  binaries match. Disassembly confirms latch preload before GPIO output enable,
  data reads before clock pulses, and eight iterations per sample.
- No calls to `configure_external_ram()` and no linked EEPROM or relay driver.

This button image was uploaded and its USB heartbeat verified. Two subsequent
captures contained no button events, so physical button scanning is not yet
verified. The first capture's overlap with the presses was uncertain.

### Raw diagnostic image

The next image retains the same GPIO initialization, read sequence and debounce
logic. Only reporting changes: all eight raw bits, uptime and scan count print
every 100 ms, alongside any decoded press/release events. Hold each button for
about one second during capture so its raw state appears in multiple reports.
Displays are not initialized and the application does not write saved settings.

`make -C cntrl/maker/morningstar/mc/firmware test inspect` passed without compiler
warnings. The decoded
HEX and ELF-derived binary match; the stock flash configuration still matches.
No Python or Python-consumed data changed, so Python checks were not run.

- Raw image: 21,504 bytes; HEX SHA-256:
  `5ec0c929632461040b5fa0ea9e85ba8d493c06e6c1a0fdb5571f426951bd6ade`.
- Raw SHA-256:
  `5f7c4cd4137beef63d175b5a041da54f6652a219e2d00f6fcb623152efc8ddcd`.

This diagnostic image was uploaded. A live capture verified exactly one press
and release for each of A-F, with the corresponding raw states:
`00111011`, `00111101`, `00111110`, `00110111`, `00101111`, `00011111`.
The released state was `00111111`. The earlier missing events remain
unexplained; they do not establish a read-sequence failure.

### Button-event image

`make -C cntrl/maker/morningstar/mc/firmware test inspect` passed without compiler
warnings. Host tests
cover hold/double-press timing, exclusions, timer wraparound and all 63 nonempty
button combinations in addition to the original debounce tests. Static image
checks still pass; GPIO initialization and the read sequence are unchanged.

- HEX SHA-256:
  `0b0cffc0dd84cac0bb2ea9d21dae13df0d9dc606fb854ad46fae029151df0d59`.
- Raw SHA-256:
  `6c36b64adb7369f7bc35c0d7a6b42d611e03276ce1ebe21122aef141e64b7774`.

The button-event image was uploaded after detecting HalfKay automatically;
USB enumeration and raw reports resumed. Live tests verified A held after
501 ms, B double-pressed 123 ms after release, and independent D/E holds and
releases. The repeated D/E test released E 543 ms before D, correctly reported.
No MIDI or saved-setting operations were added. PROGRAM remains
the established way to enter HalfKay for upload or stock recovery.

## Display image inspection, 2026-10-21

`make -C cntrl/maker/morningstar/mc/firmware test inspect` passed without compiler
warnings. Host tests
decode simulated GPIO SPI into per-panel command/data transactions, checking
selection isolation, reset timing, rotation, address offsets, complete 160x128
RGB565 images, black background, glyph pixels and different slot-number images.
Both button and display tests pass. No Python-consumed files changed.

- Raw image: 23,552 bytes; HEX SHA-256:
  `2f09ceaa5c55d3841e5a9223e83e48529b5b70dbdd0e60d5d59bb38f73055020`.
- Raw SHA-256:
  `24981e85d6e95c01a450162e5767047d7adabd5e51bb0492c597a26bb54d9faa`.
- Stock flash configuration verified; ELF-derived and HEX-derived binaries match.

This image has not yet been uploaded. Visual confirmation is needed for text,
orientation and physical slot order. USB and button regression checks are also
needed after the display initialization.

## Original USB-only image inspection, 2026-10-21

`make -C cntrl/maker/morningstar/mc/firmware inspect` completed without compiler
warnings. No device access
or upload was performed. No Python or Python-consumed data changed, so Python
tests were not run.

- Raw image: 20,480 bytes at `0x60000000..0x60004fff`.
- HEX SHA-256: `56ed52244b02824c51de185b67ffc1ec4700ce191411d67545c8808976eb86ce`.
- Raw SHA-256: `dc4a1717d3decfb42608d36b69e86bb336ef1cf1604327f142afa28db61f95d5`.
- ELF: little-endian ARM executable, Cortex-M7 hard-float build.
- ELF entry `0x60001000` is the NXP image vector table, not a Cortex-M stack
  vector. Header `0x432000d1`; Thumb reset entry `0x600015e5`.
- Boot data: base `0x60000000`, length `0x5000`, non-plugin image.
- FlexSPI configuration: 8 MiB flash, matching the stock image's declared size.
- ITCM code: 6,624 bytes; initialized DTCM: 2,752; DTCM BSS: 1,056;
  DMA/OCRAM BSS: 12,416; external-RAM BSS: zero.
- CSF placeholder at `0x60004400`: 3,072 zero bytes, no HAB signature.
- Compiled `setup()` immediately returns; the loop checks time and USB state
  and prints the heartbeat. No GPIO or storage-driver entry points appeared
  in the linked symbol list.
- Compiled startup does not call `configure_external_ram()`; USB initialization
  and software bootloader entry remain present.
- Descriptor-table pointers select the custom UTF-16 strings and the
  `16c0:0483` device descriptor.

### Flash compatibility resolved at the image level

The complete 512-byte flash configuration now matches the validated stock HEX,
including timings, all command sequences and reserved bytes. See
[the decoding and validation report](flash-configuration.md). The earlier claim
that sample-clock selection differed was incorrect: both used DQS-pad loopback.

This resolves the identified flash-configuration blocker for a controlled first
upload, not the broader question of whether independent firmware runs correctly
on the board. Static inspection does not prove boot, USB enumeration, electrical
inactivity of every board net, settings preservation, or acceptance of a custom
image by the bootloader on other hardware. This image has since passed its
first USB bring-up on this pedal. Keep the verified
stock HEX and bank backup available, with PROGRAM accessible for recovery.

## Sources

- [Arduino CLI installation](https://docs.arduino.cc/arduino-cli/installation/).
- [PJRC board package installation](https://www.pjrc.com/teensy/td_download.html).
- Installed, pinned `cores/teensy4/startup.c`, `bootdata.c`, `usb.c`, `usb_desc.h`,
  `analog.c`, `pwm.c`, linker script and `platform.txt` under
  `.tools/data/packages/teensy/hardware/avr/1.62.0/`.
