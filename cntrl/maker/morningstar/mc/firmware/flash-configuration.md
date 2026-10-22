# MC6 Pro flash configuration, 2026-10-21

## Result

The USB-only image now has the same complete 512-byte FlexSPI Configuration
Block (FCB) as the stock firmware successfully reinstalled on this pedal.
Its SHA-256 is:

`65b509525e8b0e6e92e4e6bc8fa39575399b27532e04a55f7a7883212042fe7a`

`make -C cntrl/maker/morningstar/mc/firmware inspect` checks this hash on the built
binary, after verifying
that the HEX decodes to the same bytes as the ELF. It fails on the previous
generic Teensy configuration. Building needs neither the stock HEX nor a pedal.

The configuration is described in `usb_probe/flash_config.c`, using NXP's field
offsets and instruction encodings. This deliberately reproduces hardware
metadata, not Morningstar executable code. The application remains independent.

## What differed

Words are little-endian; offsets are relative to flash address `0x60000000`.

| Field | Generic Teensy 4.1 | MC6 Pro stock and corrected target |
| --- | --- | --- |
| Read sample source, `0x0c` | 1, DQS-pad loopback | Same; no difference |
| CS hold/setup bytes, `0x0d..0x0e` | 1 / 2 | 3 / 3 |
| Serial clock enum, `0x46` | 6, requests 100 MHz | 8, requests 166 MHz |
| Custom-sequence enable, `0x47` | 1 | 0 |
| Read opcode/address, LUT 0 | `0xeb` on 1 pad, 24-bit address on 4 pads | Same |
| Remaining read instructions | Mode byte `0xff` on 4 pads, 4 dummy clocks, read operand 1 | 6 dummy clocks on 4 pads, read operand 4 |
| Status/program/erase LUT slots | Zero | Standard NOR sequences listed below |
| Capacity and geometry | 8 MiB; 256-byte page, 4 KiB sector, 64 KiB block | Same |

The old read sequence's mode byte takes two clocks on four pads. Therefore its
two mode clocks plus four dummy clocks are not simply a shorter six-clock wait:
the bus drive/mode behavior differs. The corrected sequence leaves the data
lines undriven for six dummy clocks instead of explicitly sending a mode byte.
READ/WRITE operands are LUT transfer operands, not the size of the whole image
or necessarily the length of an AHB transaction.

The previous report incorrectly included sample-source selection among the
differences. Decoding byte `0x0c`, rather than the entire packed word, corrects
that mistake. Clock frequencies above describe NXP enum requests, not measured
signals or an independent rating of the installed flash chip.

## Command table decoding

NXP encodes each 16-bit instruction as opcode in bits 15:10, pad-count code in
bits 9:8, and operand in bits 7:0. Pad codes 0 and 2 mean one and four pads.
Each 32-bit LUT word holds two instructions, low instruction first. Each slot
has four words; zero instructions stop the sequence and deassert CS.

| Slot | Corrected sequence |
| --- | --- |
| 0, read | CMD `0xeb` on 1 pad; address 24 bits on 4 pads; dummy 6 on 4 pads; READ operand 4 on 4 pads; STOP |
| 1, status | CMD `0x05`; READ operand 4; STOP, all on 1 pad |
| 3, write enable | CMD `0x06`; STOP, on 1 pad |
| 5, sector erase | CMD `0x20`; address 24 bits; STOP, on 1 pad |
| 8, block erase | CMD `0xd8`; address 24 bits; STOP, on 1 pad |
| 9, page program | CMD `0x02`; address 24 bits; WRITE operand 4; STOP, on 1 pad |
| 11, chip erase | CMD `0x60`; STOP, on 1 pad |

These are configuration data, not application calls. Providing program/erase
LUT entries does not execute them. The USB-only application never starts an
IP-command transfer or invokes a flash programming driver. Device-mode and
configuration-command enable fields remain zero, as in stock; no extra flash
status-register, quad-enable or persistent mode write is added by this change.
The exact flash manufacturer/part is still unidentified. Matching the known-good
configuration avoids inventing a part or choosing timings from an assumed one.

## Build integration and checks

`external-flash-config.patch` guards only the core's generic FCB definition.
The build defines `CNTRL_EXTERNAL_FLASH_CONFIG`, allowing the sketch's constant
512-byte definition to occupy `.flashconfig` at `0x60000000`. NXP boot data,
reset entry and the unsigned CSF placeholder still come from the Teensy core.
There is one linked FCB, not two blocks and not a post-link binary overwrite.

Patch checks use `--force --reverse --dry-run --fuzz=0`, preventing BSD patch
from automatically trying the opposite direction. Installation is idempotent;
an unapplied required patch cannot silently pass the build check.

Completed checks:

- Validated all stock HEX records using
  `cntrl.maker.morningstar.mc.firmware.read_hex`, and verified
  the source SHA-256 against `228d73e768fad383bc205c8fc1ef52f3a49de08c754aca11d4a069ca1aab9fc8`.
- Compared all 512 stock FCB bytes with the decoded new HEX: exact equality.
- Rebuilt the previous generic configuration separately; its raw SHA-256
  reproduces `c146698d6344ba1408db98f7c7a4f5dab604dd9679baae80f11ed0ce078e08e6`.
- Confirmed the new FCB hash check rejects that old generic block.
- Confirmed the reverse patch check rejects an unpatched core, and a clean
  rebuild succeeds without compiler warnings.
- Reviewed disassembly differences outside the FCB: linker-generated branch
  veneers changed order and corresponding calls changed destinations, without
  changing their target functions or introducing application operations.
- Rechecked boot base, length, reset entry, zero CSF, USB descriptors and baud-134
  bootloader entry. The image remains 20 KiB, ending at `0x60004fff`.
- Reviewed invoked core startup and clock code: it does not replace the ROM's
  FLEXSPI1 command table or CS timing configuration. The stock startup's PFD
  constants `0x2018101b` and `0x13110d0c` match the core's constants.
- The external-RAM probe remains uncalled. Its FLEXSPI2 initialization is not
  a runtime reconfiguration of the application flash on FLEXSPI1.

The rebuilt HEX SHA-256 is
`56ed52244b02824c51de185b67ffc1ec4700ce191411d67545c8808976eb86ce`;
the raw SHA-256 is
`dc4a1717d3decfb42608d36b69e86bb336ef1cf1604327f142afa28db61f95d5`.

## First-upload boundary

The specific FCB mismatch is resolved. This is ready for a controlled first
bring-up experiment, not a claim of hardware qualification. Actual boot,
custom-image acceptance and USB operation remain untested. No flashing,
bootloader command, USB query, serial connection, fuse change or device-side
memory operation was performed during this investigation.

For the eventual test, keep the backup and verified stock HEX, PROGRAM access,
and the working official uploader available. Leave external MIDI, configurable
ports and USB-host peripherals disconnected. Do not monitor at baud 134. A
missing USB device or frozen display is a recovery case, not a reason to change
security settings or blindly repeat writes. The displays are not initialized
by this image, so lack of display activity alone is not a failure.

## Primary references

- NXP's [RT1062 ROM API definitions](https://github.com/nxp-mcuxpresso/mcux-sdk/blob/8a289764d763ad06e0c3a05c885644ed98b970af/devices/MIMXRT1062/drivers/fsl_romapi.h)
  define FCB offsets, sampling/clock enums and NOR geometry.
- NXP's [FlexSPI driver definitions](https://github.com/nxp-mcuxpresso/mcux-sdk/blob/8a289764d763ad06e0c3a05c885644ed98b970af/drivers/flexspi/fsl_flexspi.h)
  define instruction opcodes, pad codes and dummy-cycle behavior.
- The pinned Teensy 1.62.0 `bootdata.c`, `startup.c`, `clockspeed.c`, `imxrt.h`
  and `imxrt1062_t41.ld` supply the generic baseline and linked startup behavior.
