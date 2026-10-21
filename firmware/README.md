# MC6 Pro USB-only experimental firmware

Build and inspection only. This image has not been flashed or run on the pedal.
It is an independent application using the public Teensy core, not a patched
Morningstar executable. It is not yet a functional MIDI controller.

## Build

On macOS Apple Silicon, with `make`, `curl`, `tar`, `patch` and `shasum`:

```bash
make -C firmware tools
make -C firmware inspect
```

Run these from the repository root. Tool paths are resolved relative to the
Makefile. The first command downloads Arduino CLI 1.5.1, verifies its pinned
SHA-256, and installs PJRC's `teensy:avr@1.62.0` package and its dependencies,
including Arm GCC 15.2.1. Everything stays in `firmware/.tools/`; Python
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
outputs are under `firmware/build/`:

- `usb_probe.ino.hex`: upload-format image, not permission to upload it.
- `usb_probe.ino.elf`: linked executable with debug information.
- `usb_probe.ino.bin`: raw flash contents.
- `symbols.txt` and `disassembly.txt`: inspection evidence.

## Application and startup boundary

The product string is `cntrl experimental`, manufacturer `cntrl`. USB Serial
uses the core's VID:PID `16c0:0483`, not Morningstar's ID. This is a private
development identity, not an allocation for a shipping product. With a serial
connection open and DTR asserted, the application emits
`cntrl experimental firmware` once per second. No serial commands are parsed.
Do not monitor at baud 134: the core interprets it as a bootloader request.

The application has no GPIO, display, MIDI, EEPROM, SD-card, flash-programming,
relay or configurable-port operations. It does not implement presets. There is
no LED blink: pin 13 is a shared hardware clock here.

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
external application flash. The application explicitly drives no output pin.

The linked USB ISR retains the CDC baud-134 request and timer leading to
`_reboot_Teensyduino_()`. That routine reads security state and uses the normal
breakpoint or secure-ROM entry path; it does not program fuses. This is static
evidence, not a successful test on this board. PROGRAM remains the established
physical recovery method.

## Inspection results, 2026-10-21

`make -C firmware inspect` completed without compiler warnings. No device access
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
image by the bootloader. No custom image has been flashed. Keep the verified
stock HEX and bank backup available, with PROGRAM accessible for recovery.

## Sources

- [Arduino CLI installation](https://docs.arduino.cc/arduino-cli/installation/).
- [PJRC board package installation](https://www.pjrc.com/teensy/td_download.html).
- Installed, pinned `cores/teensy4/startup.c`, `bootdata.c`, `usb.c`, `usb_desc.h`,
  `analog.c`, `pwm.c`, linker script and `platform.txt` under
  `.tools/data/packages/teensy/hardware/avr/1.62.0/`.
