# MC6 Pro pin and peripheral map

Static analysis of `2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex`.
SHA-256 of the HEX file:
`228d73e768fad383bc205c8fc1ef52f3a49de08c754aca11d4a069ca1aab9fc8`.

The photographed board is marked `MC6 PRO v1.2`, `2024-10-22`, with an NXP
MIMXRT1062 processor. These findings apply to this firmware, not automatically
to every hardware revision. Nothing was connected, flashed or electrically tested.

“Confirmed” below means established by tracing firmware instructions and data.
It does not mean confirmed by probing the board. Pin numbers are the firmware's
Teensy-style logical numbers, **not connector contacts or processor BGA balls**.

## Address conventions and evidence

All code addresses below are runtime ITCM addresses, unless explicitly marked
as flash. Thumb function pointers have bit zero set; instruction addresses do not.
Startup establishes these mappings:

| Runtime region | File offset in decoded binary | Size |
| --- | --- | --- |
| ITCM `0x00000000` | `0x148f4` | `0x650e8` |
| Initialized DTCM `0x20000000` | `0x79b60` | `0x86c0` |
| Flash `0x60000000` | `0` | `0x83000` |

Thus code at `a` is at binary offset `a + 0x148f4`; initialized RAM data at `a`
is at offset `a - 0x20000000 + 0x79b60`. BSS objects above the initialized DTCM
range must be reconstructed from their constructors, not read from the file.

The pin table is at runtime `0x20003100`, binary offset `0x7cc60`.
There are 55 records, each four little-endian 32-bit words:
GPIO base, mux-register address, pad-control address, GPIO bit mask.
This matches the Teensy 4.1 pin layout. It is not evidence that the pedal contains
a complete Teensy board or its bootloader.

Identified routines:

| Runtime address | Interpretation | Evidence |
| --- | --- | --- |
| `0x2606c` | Hardware-test dispatcher | Test labels, selector branches and I/O calls |
| `0x4d8cc` | Digital write | Pin bounds check, pin table and GPIO set/clear writes |
| `0x4d8d8` | Digital read | Pin table and GPIO input read |
| `0x4d8e4` | Pin mode | GPIO direction and pad/mux setup |
| `0x4d2fc` | Analog read | Channel table and ADC1/ADC2 register access |
| `0x2648` | Eight-bit switch reader | Load strobe, eight data reads and clock pulses |

## Footswitches: confirmed

The six switches are not read through six independent processor inputs. They
share an eight-bit serial input reader at `0x20012fc8`.

| Reader signal | Logical pin | GPIO | Processor pad |
| --- | --- | --- | --- |
| Parallel-load strobe, low then high | 28 | GPIO8 bit 18 | `GPIO_EMC_32` |
| Read gate, high during load, low during shifting | 29 | GPIO9 bit 31 | `GPIO_EMC_31` |
| Serial data input | 12 | GPIO7 bit 1 | `GPIO_B0_01` |
| Shift clock | 13 | GPIO7 bit 3 | `GPIO_B0_03` |

This is consistent with a 74HC165-style parallel-input shift register. The exact
chip and the electrical role of the read gate still need a readable chip marking
or continuity measurements. Do not infer them solely from the protocol shape.

The reader assembles bits from 7 down to 0. The switch objects select:

| Switch | Reader bit | Object address |
| --- | --- | --- |
| A | 2 | `0x20012d74` |
| B | 1 | `0x20012d9c` |
| C | 0 | `0x20012dc4` |
| D | 3 | `0x20012dec` |
| E | 4 | `0x20012e14` |
| F | 5 | `0x20012e3c` |

A zero bit is interpreted as pressed. Bits 6 and 7 have no established function.

Trace: reader construction `0x28a54` onward; switch construction `0x28ac6`
through `0x28c14`; ordered switch registration `0x3231a` through `0x32348`;
bit extraction and active-low comparison at `0x3b54` and `0x506c`.
The switch test at `0x274d2` polls the six indices through `0x4b070` using
the container at `0x20012fdc`.

## Relay outputs: confirmed

| Test action | Logical pin | GPIO | Processor pad | Object-pointer location | Test write |
| --- | --- | --- | --- | --- | --- |
| A tip | 35 | GPIO7 bit 28 | `GPIO_B1_12` | `0x20012e88` | `0x26310` |
| A ring | 2 | GPIO9 bit 4 | `GPIO_EMC_04` | `0x20012e84` | `0x26416` |
| B tip | 40 | GPIO6 bit 20 | `GPIO_AD_B1_04` | `0x20012e90` | `0x2651c` |
| B ring | 41 | GPIO6 bit 21 | `GPIO_AD_B1_05` | `0x20012e8c` | `0x26644` |

Each test toggles its object's stored state, loads the pin from byte offset 1,
calls digital write, and displays the corresponding relay label. Constructors
at `0x28976` through `0x28a42` assign the four pins; initialization around
`0x176e2` configures outputs and writes their stored states.

Unknown: whether HIGH means a closed jack contact, driver inversion, power-up
contact state, and relay-coil circuitry. The names above are firmware labels,
not a continuity-tested contact schematic.

## Configurable ports in expression mode: confirmed

| Port | ADC input pin | Processor pad | ADC channel | Driven-high pin | Processor pad |
| --- | --- | --- | --- | --- | --- |
| 1 | 26 | `GPIO_AD_B1_14` | ADC2 channel 3 | 32 | `GPIO_B0_12` |
| 2 | 38 | `GPIO_AD_B1_12` | ADC2 channel 1 | 9 | `GPIO_B0_11` |
| 3 | 39 | `GPIO_AD_B1_13` | ADC2 channel 2 | 37 | `GPIO_B1_03` |
| 4 | 14 | `GPIO_AD_B1_02` | ADC1 channel 7 | 36 | `GPIO_B1_02` |

Setup at `0x3228a` through `0x322da` calls constructor `0x4a8b0` with these
pairs. Object pointers are stored at `0x20012d64`, `0x20012d68`, `0x20012d6c`
and `0x20012d70` respectively. Constructor fields `+0x18` and `+0x19` hold
the two pins.

Mode setup `0x4a854`, zero-mode branch `0x4a878`, configures the first pin
as input and the second as output HIGH. Reader `0x4a568`, call at `0x4a57e`,
passes the first pin to analog read. The channel-byte table at `0x20003594`
contains `0x83`, `0x81`, `0x82`, `0x07` for the four inputs; bit 7 selects ADC2.
ADC1 and ADC2 bases are `0x400c4000` and `0x400c8000`.

The expression test references these same objects. Another mode configures
the first pin with an input pull-up. This does not yet establish the complete
external-switch or MIDI modes.

Unknown: which conductor is tip/ring for each signal, the supply voltage and
protection network, and configurable-port MIDI routing. A GPIO driven HIGH must
not be treated as a characterized power supply or safe arbitrary-voltage input.

## MIDI UARTs: confirmed startup pins and peripherals

| Test label | MIDI object | Serial object | UART | Base | Startup RX / TX pins |
| --- | --- | --- | --- | --- | --- |
| DIN5 | `0x200101a0` | `0x200074f4` | LPUART6 | `0x40198000` | 0 / 1 |
| 3.5MM | `0x20010a30` | `0x20007558` | LPUART4 | `0x40190000` | 7 / 8 |

Construction at `0x27d38` and `0x27d50` links each MIDI object to its serial
object. Each serial object's UART base is at offset `+0x14`, and its hardware
description pointer is at `+0x18`:

- LPUART6 descriptor `0x20001798`: default RX 0, TX 1; alternatives RX 52, TX 53.
- LPUART4 descriptor `0x20007980`: default RX 7, TX 8; alternate entries disabled.

The test polls the two MIDI objects at `0x26fdc` and `0x26f9a`. Their success
paths at `0x270ec` and `0x27144` display `DIN5 Received!` and `3.5MM Received!`
respectively. This establishes the jack-family-to-UART associations.

The initialization path now establishes that these default entries are actually
selected at normal startup. Calls at `0x31472` and `0x3147a` initialize the two
MIDI objects through `0x1bf94`. That routine follows their transport pointers and
calls serial begin at `0x50a40` with 31,250 baud and format zero (8N1).

Serial begin reads RX/TX descriptor indices from object bytes `+0x1c` / `+0x1d`.
Both are zero in each initialized serial object. RX pad/mux/input-select writes
are at `0x50b22` through `0x50b58`; TX writes at `0x50b5a` through `0x50b8e`.
No intervening pin-index change was found in the traced startup path.

| Signal | Logical pin | Processor pad | Mux / input-select value |
| --- | --- | --- | --- |
| DIN RX, LPUART6 | 0 | `GPIO_AD_B0_03` | 2 / 1 |
| DIN TX, LPUART6 | 1 | `GPIO_AD_B0_02` | 2 / 1 |
| 3.5 mm RX, LPUART4 | 7 | `GPIO_B1_01` | 2 / 2 |
| 3.5 mm TX, LPUART4 | 8 | `GPIO_B1_00` | 2 / 2 |

This establishes startup configuration, not optocoupler/driver circuitry,
connector-contact continuity or the absence of later changes in every mode.

## USB: confirmed controller roles

| Firmware path | Controller | Register base | PHY base | Role setup |
| --- | --- | --- | --- | --- |
| USB device | USB1 | `0x402e0000` | `0x400d9000` | Flash `0x60001f92` |
| USB host | USB2 | `0x402e0200` | `0x400da000` | ITCM `0x42c86` |

Device initialization begins at flash `0x60001f0c`. It resets USB1 and initializes
USBPHY1, then writes USBMODE `0x0a`: controller-mode bits are 2 (device).
It installs ISR `0x4eb9c` at vector-table offset `0x204`, IRQ 113.

Normal startup calls host initialization `0x42b9c` at `0x3140a`. It initializes
USBPHY2, resets USB2, writes USBMODE 3 (host), installs ISR `0x43314` at
vector-table offset `0x200`, IRQ 112, and sets the port-power bit in PORTSC1
at `0x42c9e` through `0x42ca8`.

Host startup also configures an additional output that is not present in the
55-entry logical-pin table:

| Signal | Pad | GPIO | Mux register | Pad-control register | Startup level |
| --- | --- | --- | --- | --- | --- |
| Host-startup output, likely VBUS enable | `GPIO_EMC_40` | GPIO8 bit 26 | `0x401f80b4` | `0x401f82a4` | HIGH |

Instructions `0x42c22` through `0x42c30` select mux 5, set pad control `0x08`,
enable the GPIO output and drive it HIGH before the controller starts. This
matches the public Teensy 4.1 host initialization sequence. Its actual connection
to a power switch, current limit and polarity at the socket remain unverified.
Do not confuse this GPIO output with the pad's alternative overcurrent function.

The MIDI test's host reception routine `0xb45c` calls the host task dispatcher
`0x43bd0` before polling its MIDI objects. Device reception `0x4fc58` consumes
USB-MIDI packets through `0x4fbc8`; that routine masks/unmasks IRQ 113 while
manipulating the receive queue, matching USB1's installed interrupt.

The USB data pairs use the dedicated controller/PHY connections, not ordinary
digital-pin numbers. Unknown: physical socket-contact routing, USB-C CC circuitry,
VBUS sensing/protection, and whether the extra output actually controls socket
power on this board revision. Software role selection is not a continuity test.

## Displays: confirmed firmware wiring

The three display drivers use GPIO-driven software SPI, not the LPSPI4 object
listed below. They share data and clock, with individual CS, D/C and reset pins.
No MISO input is configured for these drivers.

| Display slot / pointer location | Constructor call | CS pin | D/C pin | Reset pin |
| --- | --- | --- | --- | --- |
| 1 / `0x20010194` | `0x12b16` | 15 | 34 | 21 |
| 2 / `0x20010198` | `0x12c40` | 24 | 23 | 22 |
| 3 / `0x2001019c` | `0x12cbc` | 27 | 25 | 20 |

Slot numbers describe software order, not a verified left/centre/right order.
Each pointer refers to a UI wrapper; its display-driver pointer is at `+0x2c`.
The controller stores the wrappers at `+0x1b0`, `+0x1b4`, `+0x1b8`, then aliases
them at `+0x158`, `+0x15c`, `+0x160` at `0x12d12` through `0x12d2c`.
Startup copies those aliases to the three globals at `0x31994` through `0x319b6`.

| Signal | Logical pin | GPIO | Processor pad |
| --- | --- | --- | --- |
| Shared data / MOSI | 11 | GPIO7 bit 2 | `GPIO_B0_02` |
| Shared clock | 13 | GPIO7 bit 3 | `GPIO_B0_03` |
| Slot 1 CS | 15 | GPIO6 bit 19 | `GPIO_AD_B1_03` |
| Slot 1 D/C | 34 | GPIO7 bit 29 | `GPIO_B1_13` |
| Slot 1 reset | 21 | GPIO6 bit 27 | `GPIO_AD_B1_11` |
| Slot 2 CS | 24 | GPIO6 bit 12 | `GPIO_AD_B0_12` |
| Slot 2 D/C | 23 | GPIO6 bit 25 | `GPIO_AD_B1_09` |
| Slot 2 reset | 22 | GPIO6 bit 24 | `GPIO_AD_B1_08` |
| Slot 3 CS | 27 | GPIO6 bit 31 | `GPIO_AD_B1_15` |
| Slot 3 D/C | 25 | GPIO6 bit 13 | `GPIO_AD_B0_13` |
| Slot 3 reset | 20 | GPIO6 bit 26 | `GPIO_AD_B1_10` |
| Backlight PWM control | 6 | GPIO7 bit 10 | `GPIO_B0_10` |

The constructor at `0x49858` takes CS, D/C, MOSI, clock and reset, in that order.
It sets a native geometry of 128 by 160 and passes MISO as -1. The chain through
`0x49a9c` to `0x48b84` stores the pins and GPIO set/clear register pointers.
The SPI-type field at driver offset `+0x64` is set to 1. The matching byte-write
branch at `0x48e48` shifts eight bits through those GPIO registers, establishing
software SPI rather than merely inferring it from constructor arguments.

All three drivers call initialization at `0x4988c` with argument zero. Its
structure matches Adafruit's ST7735 `initR` path, including green-tab address
offsets. Firmware command tables are at flash `0x60013828`, `0x60013864` and
`0x60013894`. They configure ST7735-family registers, 16-bit pixel data
(`COLMOD 0x05`), columns 2 through 129 and rows 1 through 160, then turn the
display on. Timing, power and gamma values differ from the current upstream
defaults. This identifies the driver family and configuration, not the exact
physical controller revision, panel size or manufacturer.

Reset initialization at `0x48cfc` through `0x48d34` drives the reset pin HIGH,
LOW, HIGH with delays of 100, 100 and 200 ms. The wrappers subsequently request
rotation 1 through the vtable entry for `0x4975c`, so the configured drawing
orientation is landscape, 160 by 128.

Backlight identification is no longer tentative. Startup configures pin 6 and
writes PWM value 5 at `0x31490` / `0x3149a`. Brightness handling at `0x45fc`
through `0x4628` reads setting `0x62`, clamps it to 0 through 7, then writes
`32 * setting + 31` to pin 6 through `0x4e324`. The settings-write path repeats
that calculation at `0x1ad14` through `0x1ad40`. The dimming path writes 31
at `0x45c6` through `0x45d0`.

Pin 13 is also the switch-reader clock. A replacement driver must account for
that shared connection. The switch reader reloads its parallel inputs before
each eight-bit sample; display clock edges must not be treated as switch scans.

Unknown: panel positions, connector contact numbering, backlight driver circuit
and voltage/current requirements. None of these connections has been probed.

## EEPROM: confirmed external path and flash-emulation alternative

Normal startup explicitly selects an external EEPROM, not flash emulation:

| Property | Recovered value | Evidence |
| --- | --- | --- |
| Seven-bit I²C address | `0x54` | Constructor argument at `0x31420` |
| Configured write-page size | 128 bytes | Constructor argument at `0x3141e` |
| Read / write address bytes | High byte, then low byte | `0x3aab0`, `0x3ab68` |
| I²C object | `0x200076fc` | EEPROM constructor and read/write routines |
| Peripheral | LPI2C3, base `0x403f8000` | Object field `+0x10` |
| SDA | Pin 17, `GPIO_AD_B1_06` | Hardware descriptor and begin-time mux setup |
| SCL | Pin 16, `GPIO_AD_B1_07` | Hardware descriptor and begin-time mux setup |

SDA and SCL correspond to GPIO6 bits 22 and 23 when used as GPIO. The I²C driver
instead assigns their peripheral mux functions. Its hardware descriptor is at
flash `0x60013720`: first SDA entry is pin 17, mux `0x11`, input-select value 2;
first SCL entry is pin 16 with the same mux and input-select value. Alternate
entries are pins 44 and 45. Initialized SDA/SCL indices at object offsets
`+0x18` / `+0x19` are both zero. The traced constructor does not change them.

At `0x31424`, startup calls EEPROM constructor `0x3aa28`, which saves address
`0x54` and page size `0x80`, then initializes the I²C object via the trampoline
at `0x64e88` to flash function `0x60001654`. That function selects the indexed
SDA/SCL descriptor entries and writes their pad/mux/input-select registers.
Startup stores the EEPROM pointer in `0x2001018c`, sets byte `+4` to 1 and
byte `+5` to 0 at `0x3143a` through `0x31442`, selecting the external path.

Single-byte read `0x3aaf8` first sends the memory address through `0x3aab0`,
then requests one byte. Single-byte write `0x3abb4` uses `0x3ab68` to start
the address/data transaction and finishes through `0x3aa90`, which also polls
for device readiness. The base address produces wire address bytes `0xa8`
for write and `0xa9` for read. These traces establish a 24xx-style I²C EEPROM
interface, but not an exact chip model or capacity. A 128-byte page is a software
configuration, not an independent measurement of the installed part.

The storage wrapper also supports flash-backed EEPROM emulation. Its static
constructor at `0x27d10` initially selects that path, but normal startup above
replaces it. Emulation routines `0x4d9c8`, `0x4da10` and `0x4dc1c` use the
flash tail beginning at `0x607c0000`, with sectors below `0x60800000`.
Do not infer a separate EEPROM chip merely from calls to these core routines.

### What the EEPROM test actually does

The hardware-test dispatcher calls `0x17010` at `0x271e2`. This routine:

1. Reads bytes 0 through 7 into a temporary buffer, then rereads and compares.
2. Writes values 0 through 7 to those addresses and verifies them.
3. Restores the saved bytes, verifies them and returns success.

Each operation dispatches through the storage wrapper's selected backend.
**This is a write test, not a harmless connectivity probe.** A mismatch returns
failure immediately; in particular, a failure during pattern verification can
return before restoring the original bytes. It was not executed during analysis.

Unknown: exact EEPROM part, installed capacity, voltage, pull-ups, write-protect
wiring and PCB connector/test-point locations. Firmware initialization does not
prove that a device is present and responding on a particular physical board.

## SD card: confirmed native four-bit interface

Normal startup calls `0x3bb88` at `0x319b8` with argument `0xfe`, selecting
the native SD branch at `0x3bc14`, not the SPI branch. The card/filesystem object
is at `0x200112c0`. That branch calls `0x419cc`, whose register accesses establish
USDHC1 at `0x402c0000`.

| SD signal | Logical pin | Processor pad | Mux register | GPIO when not muxed to USDHC |
| --- | --- | --- | --- | --- |
| CMD | 45 | `GPIO_SD_B0_00` | `0x401f81bc` | GPIO8 bit 12 |
| CLK | 44 | `GPIO_SD_B0_01` | `0x401f81c0` | GPIO8 bit 13 |
| DATA0 | 43 | `GPIO_SD_B0_02` | `0x401f81c4` | GPIO8 bit 14 |
| DATA1 | 42 | `GPIO_SD_B0_03` | `0x401f81c8` | GPIO8 bit 15 |
| DATA2 | 47 | `GPIO_SD_B0_04` | `0x401f81cc` | GPIO8 bit 16 |
| DATA3 | 46 | `GPIO_SD_B0_05` | `0x401f81d0` | GPIO8 bit 17 |

Initialization first sets these pads to GPIO mode, then `0x411b4` selects mux 0
on all six pads and configures their pad controls. The SD command sequence
subsequently requests four-bit operation at `0x41c2a` through `0x41c2e`, then
sets USDHC1 PROT_CTRL data-width bits to 1 at `0x41c38` through `0x41c50`.
Its ISR is `0x40f44`, installed at vector-table offset `0x1f8`, IRQ 110.

The wrapper at `0x3bb88` also contains an SPI-card alternative, with a default
25 MHz requested clock and caller-supplied CS. That is not the branch selected
by this startup. Likewise, an initialized LPSPI4 object is not evidence that the
installed card uses SPI.

On a card-initialization or filesystem-mount failure, the wrapper configures
pin 46 as an input with pull-down at `0x3bc38` through `0x3bc48` and records it
in a card-status field. This is the DATA3 pad reused as GPIO, not evidence of
a separate mechanical card-detect switch. The exact later detection behavior
and any mechanical switch connection remain TODO.

Unknown: physical microSD contact continuity, power switching, protection and
installed-card capacity. No card was accessed during this analysis.

## Other peripheral objects: incomplete

Initialized objects also contain these peripheral bases:

| Data location containing base | Peripheral | Base |
| --- | --- | --- |
| `0x20007494` | LPSPI4 | `0x403a0000` |
| `0x200075cc` | LPI2C1 | `0x403f0000` |
| `0x2000770c` | LPI2C3, traced EEPROM path above | `0x403f8000` |
| `0x2000784c` | LPI2C4 | `0x403fc000` |

Except for the EEPROM path above, presence in initialized data does not prove
use. TODO: establish any board-specific users of the remaining SPI/I²C objects.
Neither the traced display path nor the selected SD-card path uses LPSPI4.

## Rechecking the evidence

Use `inspect_firmware` as described in [firmware-analysis.md](firmware-analysis.md)
to obtain the decoded binary and flash-address ELF. For example, the A-tip relay
write path starts at ITCM `0x262f8`, corresponding to flash `0x6003abec`:

```bash
llvm-objdump --disassemble-all --triple=thumbv7em-none-eabi \
  --start-address=0x6003abec --stop-address=0x6003ac14 \
  /tmp/mc6-analysis/firmware.elf
```

That ELF still uses flash addresses. Subtract `0x600148f4` from displayed ITCM
instruction addresses and relative branch targets to compare with this map.
Literal values that point into DTCM retain their runtime RAM addresses.
Do not interpret literal pools as executable instructions.

Reference layouts used to interpret the binary:
[digital I/O](https://github.com/PaulStoffregen/cores/blob/master/teensy4/digital.c),
[Teensy 4.1 pin definitions](https://github.com/PaulStoffregen/cores/blob/master/teensy4/core_pins.h),
[serial object layouts](https://github.com/PaulStoffregen/cores/blob/master/teensy4/HardwareSerial.h),
and [peripheral registers](https://github.com/PaulStoffregen/cores/blob/master/teensy4/imxrt.h).
Display comparisons:
[ST7735 constructors and initialization](https://github.com/adafruit/Adafruit-ST7735-Library/blob/master/Adafruit_ST7735.cpp)
and [ST77xx initialization](https://github.com/adafruit/Adafruit-ST7735-Library/blob/master/Adafruit_ST77xx.cpp).
I²C comparison:
[Teensy Wire implementation](https://github.com/PaulStoffregen/Wire/blob/master/WireIMXRT.cpp).
Additional comparisons:
[USB host initialization](https://github.com/PaulStoffregen/USBHost_t36/blob/master/ehci.cpp),
[native SD driver](https://github.com/PaulStoffregen/SdFat/blob/master/src/SdCard/SdioTeensy.cpp),
and [NXP RT1062 pin mux definitions](https://github.com/nxp-mcuxpresso/mcux-sdk/blob/master/devices/MIMXRT1062/drivers/fsl_iomuxc.h).
These are comparison sources, not recovered Morningstar source or proof of an
exact core-library version.

This map is useful for a replacement driver, but is not yet a complete schematic
or evidence that flashing replacement firmware is recoverable. Bootloader,
security configuration, flash layout constraints and recovery remain separate
unresolved questions.
