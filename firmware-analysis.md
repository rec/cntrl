# MC6 Pro firmware analysis

`cntrl.firmware` contains the Intel HEX inspection and analysis-artifact code.
It reads files without connecting to or programming hardware. It has no extra
dependencies or command-line interface.

```python
from pathlib import Path

from cntrl.firmware import inspect_firmware

image = inspect_firmware(
    Path('2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex'),
    Path('/tmp/mc6-analysis'),
)
```

Use a new output directory for each run. Existing artifact files are not
overwritten. The function prints its findings and writes:

- `firmware.bin`: decoded image, with gaps filled with `0xff`.
- `firmware.elf`: ELF32 ARM wrapper, with the original bytes mapped at their
  flash addresses and the reset vector as its entry point.
- `strings.txt`: printable ASCII strings and their flash addresses.

`read_hex(path)` separately returns the decoded image, populated address ranges,
HEX entry address, record counts and SHA-256 hash. It validates record lengths,
checksums, overlapping records and EOF, and supports the record types used by
this firmware, including extended linear and segment addresses.

The ELF is an analysis artifact, not a file to upload to the pedal. It does not
reconstruct the code and data copied into RAM. Explicit Thumb disassembly works
for flash-resident startup code, for example:

```bash
llvm-objdump --disassemble-all --triple=thumbv7em-none-eabi \
  --start-address=0x60001428 --stop-address=0x60001530 \
  /tmp/mc6-analysis/firmware.elf
```

## Initial findings, 2026-10-20

Input filename: `2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex`.

SHA-256: `228d73e768fad383bc205c8fc1ef52f3a49de08c754aca11d4a069ca1aab9fc8`.

- All Intel HEX record checksums pass.
- There are 536,576 decoded bytes (524 KiB), with no gaps, at
  `0x60000000` through `0x60082fff`.
- The FlexSPI configuration specifies 8 MiB of flash. This is the image's
  configuration, not an independent measurement of the physical flash chip.
- The HEX start address is `0x60001000`, the image vector table. Its reset
  vector is `0x60001429`, meaning Thumb code starting at `0x60001428`.
- Startup disassembles coherently. It resembles the public Teensy 4.x startup
  implementation, including FlexRAM initialization and copying code into ITCM.
- Startup's first copy call uses destination `0x00000000`, source `0x600148f4`
  and destination end `0x000650e8`. The following data copy uses destination
  `0x20000000`, source `0x60079b60` and destination end `0x200086c0`.
- The vector table's CSF pointer is `0x60082400`. The remaining 3,072 bytes are
  all zero. There is no HAB signature in that reserved area.
- Strings include LCD, MIDI, relay, EEPROM, switch and expression-input tests,
  firmware-upload status, and the path `firmware/mc6pro.hex`.

Raw searches for flash-address pointers to selected test strings found none.
That does not mean the strings are unused: startup relocates data, so runtime
references need their corresponding RAM addresses.

These findings establish that executable code is readable. They do not establish
the bootloader implementation, fuse settings, acceptance of custom firmware, or
guaranteed recovery. The supplied HEX file is not included in this commit.

Reference implementations:
[Teensy boot data](https://github.com/PaulStoffregen/cores/blob/master/teensy4/bootdata.c)
and [startup](https://github.com/PaulStoffregen/cores/blob/master/teensy4/startup.c).
