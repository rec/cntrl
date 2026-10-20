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

## MIDI UARTs: confirmed peripherals, candidate default pins

| Test label | MIDI object | Serial object | UART | Base | Default RX / TX pins |
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

The descriptor pins are supported/default choices, not proof of the final mux
selection. TODO: trace all serial initialization and pin-remapping calls before
treating them as confirmed board wiring. Default pad names are `GPIO_AD_B0_03`
(RX0), `GPIO_AD_B0_02` (TX1), `GPIO_B1_01` (RX7), `GPIO_B1_00` (TX8).

USB host reception is checked through `0xb45c`; USB device reception through
`0x4fc58`. Their test paths display separate USB host/device success messages.
TODO: establish USB controller instances, socket wiring and host-power control.

## Displays and other peripherals: incomplete

Three display-associated object pointers occur at `0x20010194`, `0x20010198`
and `0x2001019c`. Shared UI routines, including `0x188a0`, use all three.
This does not yet identify the display controllers or their physical order.

Pin 6 is configured as an output at `0x31490`, then passed with value 5 to a
PWM-style output routine at `0x3149a`. It is a backlight candidate only.
TODO: trace the brightness-setting callback to establish its purpose.

Other initialized objects contain these peripheral bases:

| Data location containing base | Peripheral | Base |
| --- | --- | --- |
| `0x20007494` | LPSPI4 | `0x403a0000` |
| `0x200075cc` | LPI2C1 | `0x403f0000` |
| `0x2000770c` | LPI2C3 | `0x403f8000` |
| `0x2000784c` | LPI2C4 | `0x403fc000` |

Presence in initialized data does not prove use. TODO: trace bus initialization,
display command streams and EEPROM transactions. Display CS/DC/reset pins, bus
sharing, EEPROM identity/address, and any SD-card interface remain unknown.
The EEPROM test label alone does not identify an external EEPROM or its bus.

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
These are comparison sources, not recovered Morningstar source or proof of an
exact core-library version.

This map is useful for a replacement driver, but is not yet a complete schematic
or evidence that flashing replacement firmware is recoverable. Bootloader,
security configuration, flash layout constraints and recovery remain separate
unresolved questions.
