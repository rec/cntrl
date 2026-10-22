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

Independent implementation: `firmware/usb_probe/usb_probe.ino` now reproduces
this read sequence with a 3 ms scan interval and 15 ms per-button debounce.
The stock reader uses 3 us load-low, load-high and clock-high waits; our faster
inline GPIO implementation also adds a 3 us clock-low wait. Display CS pins
15, 24 and 27 are held HIGH before clocking pin 13. Stock display initialization
at `0x48c90` onward establishes HIGH as the deselected initial level.
Build, image inspection and host debounce tests pass. A live USB capture on the
independent raw-diagnostic image confirmed A-F in order, one press and release
each, with exactly the active-low bit changes listed above. Released samples
were `00111111`; no physical interpretation is assigned to bits 6 and 7.

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
the first pin with an input pull-up. MIDI uses the same pin pairs, as traced
below.

Matching the MIDI mode branches to the documented connector configurations
identifies the ADC/first pin as **tip** and the driven-high/second pin as **ring**.
This is a software-plus-documentation inference, not continuity verification.
Unknown: the supply voltage and protection network. A GPIO driven HIGH must
not be treated as a characterized power supply or safe arbitrary-voltage input.

## Configurable ports: MIDI routing and mode selection

The stock configurable-port MIDI paths are **outputs only**. They are distinct
from the dedicated DIN and 3.5 mm MIDI inputs described below. Morningstar's
[MC6 Pro manual](https://help.morningstar.io/en/article/mc6-pro-user-manual-psevjy/)
lists MIDI output, expression input and auxiliary-switch input for these jacks,
not MIDI input. Firmware construction agrees: every serial instance created by
the configurable-port initializer has its RX pin explicitly set to `-1`.

### Signal and peripheral assignments: confirmed in firmware

Each jack has two software-controlled signals. The contact names retain the
inference qualification above. The firmware uses FlexIO timers and shifters to
generate serial output, not the dedicated LPUART peripherals and not a
CPU-timed GPIO bit-banging loop.

| Port | Contact | Logical pin | GPIO | Processor pad | Selected FlexIO signal | Mux write |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Tip | 26 | GPIO6 bit 30 | `GPIO_AD_B1_14` | FlexIO3 D14 | `0x19` |
| 1 | Ring | 32 | GPIO7 bit 12 | `GPIO_B0_12` | FlexIO2 D12 | `0x14` |
| 2 | Tip | 38 | GPIO6 bit 28 | `GPIO_AD_B1_12` | FlexIO3 D12 | `0x19` |
| 2 | Ring | 9 | GPIO7 bit 11 | `GPIO_B0_11` | FlexIO2 D11 | `0x14` |
| 3 | Tip | 39 | GPIO6 bit 29 | `GPIO_AD_B1_13` | FlexIO3 D13 | `0x19` |
| 3 | Ring | 37 | GPIO7 bit 19 | `GPIO_B1_03` | FlexIO2 D19 | `0x14` |
| 4 | Tip | 14 | GPIO6 bit 18 | `GPIO_AD_B1_02` | FlexIO3 D2 | `0x19` |
| 4 | Ring | 36 | GPIO7 bit 18 | `GPIO_B1_02` | FlexIO2 D18 | `0x14` |

Mux values include SION (`0x10`): alternate function 9 for FlexIO3 and 4 for
FlexIO2. These are writes to the processor mux registers, not MIDI mode IDs.
Pins 36 and 37 also have FlexIO3 alternatives, but the firmware's mapping routine
searches FlexIO1, FlexIO2, then FlexIO3 and selects FlexIO2 first for them.

| Handler object | Peripheral base | Pin descriptor | IRQ | ISR |
| --- | --- | --- | --- | --- |
| `0x200079e4` | FlexIO1 `0x401ac000` | `0x20001e9c` | 90 | `0x49b74` |
| `0x200079f4` | FlexIO2 `0x401b0000` | `0x20001ef8` | 91 | `0x49ba4` |
| `0x20007a04` | FlexIO3 `0x42020000` | `0x20001f54` | 156 | `0x49bd4` |

FlexIO1 is included to make the search order reproducible; none of the port
signals selects it. The handler-pointer list is at `0x20007a28`. Each descriptor
contains 22 logical-pin bytes at `+0x18`, corresponding FlexIO signal numbers
at `+0x2e`, and mux bytes at `+0x44`. Mapper `0x49c04` selects the handler;
`0x49c8c` applies its mux byte through the existing GPIO pin table.

### Persisted modes and output initialization

The per-port serial-wrapper pointers are at `0x20012fb8`, `0x20012fbc`,
`0x20012fc0`, `0x20012fc4`, in port order. Construction at `0x315e0` through
`0x31684` stores tip/ring pins at wrapper bytes `+8` / `+9` and the zero-based
physical port number at `+0x0a`. This is separate from the expression objects.

Startup calls mode setup `0x17e5c` from `0x32a4e`. The four mode bytes are
read from EEPROM offsets `0x1e` through `0x21`, using `0xd81c` and `0x3c1c`.
The selector disables expression polling initially. In a MIDI branch it also
disables the two corresponding auxiliary-switch entries, initializes the serial
wrapper, and appends it to the active-output array at `0x20012620`; the count
is at `0x200133b8`. Disabling a switch entry through `0x4b05c` only clears its
enabled byte. It does **not** change the processor pin's direction or mux.

The following table separates the EEPROM mode value from the argument passed
to serial initializer `0xae84`. Documented names are matched by conductor use.

| EEPROM mode | Initializer argument | Serial TX conductor | Explicit action on the other conductor | Interpretation |
| --- | --- | --- | --- | --- |
| 4 or 5 | 0 | Tip | Ring set to GPIO output HIGH | Standard-style output; both values take the same branch |
| 6 | 1 | Tip | No change in this initializer | Tip-active output |
| 7 | 2 | Ring | No change in this initializer | Ring-active output |
| 9 | 3 | Ring | Tip set to GPIO output HIGH | Type-B-style output |

Port 1 selector evidence: `0x17f56` through `0x17f80`; standard branch
`0x17fd0` through `0x17ffe`; tip-active `0x1842a`; ring-active `0x18582`;
Type B `0x18616`. Equivalent branches configure ports 2 through 4. Mode 1
enables expression input instead. Mode 10 follows a separate GPIO-control
path through `0x15540`, not a MIDI receive path.

Initializer evidence: argument 0 at `0xaed0` drives ring HIGH, then constructs
a tip transmitter; argument 1 at `0xb040` constructs a tip transmitter;
argument 2 at `0xafec` constructs a ring transmitter; argument 3 at `0xafdc`
drives tip HIGH and joins argument 2. The standard branch also drives tip HIGH
before assigning it to FlexIO. All four call serial begin `0x4a10c` with
31,250 baud and format zero, with one-millisecond delays around initialization.

There is also an argument-4 implementation at `0xaf42` that constructs two
transmitters, one per conductor, and writes each queued byte to both. No caller
selecting argument 4 was found in the traced port-mode selector. Its presence
is not evidence of an exposed dual-output mode or MIDI reception.

**Floating-conductor caveat:** the manual describes the unused conductor in
tip-active/ring-active modes as floating. The serial initializer does not
explicitly make that conductor an input or disconnect it. Expression-object
construction earlier in startup calls `0x4a854`, initially making tip an input
and ring an output HIGH. Later expression-polarity setup at `0x327b4` through
`0x327d8` can change tip to input-with-pull-up; it does not explicitly release
ring. Disabling expression polling at `0x4a538` also leaves pin configuration
alone. Thus these output-mode names must not be translated into an assumption
that the unused jack contact is electrically isolated. This discrepancy needs
electrical validation; the trace alone cannot characterize circuitry between
processor and jack, or establish a hardware fault.

### Serial timing, queues and interrupt path

Each selected transmitter allocates a FlexIO timer and shifter at runtime.
Do not assign a fixed shifter number to a jack: it depends on allocation order
and other users. Allocation routines are `0x49cc8` and `0x49d10`.

Serial begin `0x4a10c` computes `7,500,000 / baud - 1`, giving `0xef` at
31,250 baud. The traced TX setup programs:

| Register | Value written |
| --- | --- |
| `SHIFTCFG[s]` | `0x32`, serial start/stop configuration |
| `SHIFTCTL[s]` | `0x30002 \| ((t & 3) << 24) \| (signal << 8)` |
| `TIMCMP[t]` | `0x0fef` |
| `TIMCFG[t]` | `0x2222` |
| `TIMCTL[t]` | `0xc00001 \| (signal << 8) \| ((4*s + 1) << 24)` |
| `CTRL` | `1`, enable FlexIO |

Here `s` is the allocated shifter, `t` the timer, and `signal` the D-number in
the pin table above. Writes are at `0x4a19a` through `0x4a208`; pin mux and
callback registration follow at `0x4a210` and `0x4a21a`. The constants and
control flow match the public FlexSerial implementation's 8N1 TX configuration.
They do not prove an exact library revision or measured baud accuracy.

The byte path is:

1. MIDI encoders select active wrappers and enqueue bytes in a 256-byte ring
   at wrapper `+0x12`, using write/read indices at `+0x0e` / `+0x10`.
   One slot is reserved to distinguish full from empty: 255 bytes are usable.
   Enqueue helper `0xb054` drains a byte synchronously if the queue fills.
2. Drain routine `0xc674` feeds queued bytes into each wrapper's transmitter.
   A call at `0x25820` requests up to 16 queued bytes per active port per call.
3. FlexSerial write `0x4a0a4` places each byte in a separate 128-slot TX ring
   and enables interrupt service. Its write/read indices are at serial-object
   `+0xa6` / `+0xa8`; the byte buffer starts at `+0x26`.
4. FlexIO ISR dispatch reaches callback `0x49f2c`, which moves bytes to
   `SHIFTBUF[s]` at peripheral offset `0x200 + 4*s`. FlexIO generates the bits.

MIDI clock has special handling. Routine `0xc5fc` emits `0xf8` directly, or
sets wrapper byte `+0x0c` while a queue write is in progress (`+0x0b`). The
drainer emits a pending clock before continuing ordinary bytes. This is not
an input buffer or a MIDI-clock receive mechanism.

Program Change is a particularly clear routing example: `0xb9b0` extracts
destination bits 2 through 5 from its mask, tests the wrapper's physical-port
number at `+0x0a`, then enqueues a `0xc0` channel status and seven-bit value
at `0xba06` and `0xba10`. Active-array position is not always the physical
port number, because non-MIDI ports are absent from that array.

### SysEx: implemented path, contrary to the manual

The manual says these configurable outputs cannot transmit SysEx. However,
this image contains a connected configurable-output SysEx encoder at `0xb720`:
it selects active outputs, optionally enqueues `0xf0` at `0xb88c`, copies the
supplied bytes, and optionally enqueues `0xf7` at `0xb820`. With argument
`r2 == 0` it supplies framing; otherwise it copies already-framed data.
It uses the same queues and transmitters as channel messages, so the 256-byte
queue is not a whole-message-length limit; full-queue paths drain while copying.

This is not merely an isolated library routine. The multi-transport sender
at `0x1b370` calls it at `0x1b394`, and sender `0x15568` conditionally tail-calls
it at `0x15606`. Other direct calls occur at `0x1b882`, `0x1b9ac`, `0x1bd68`
and `0x1be6e`. Its presence and callers establish an implemented software path,
not that every editor operation exposes it, every destination mask is correct,
or long transfers work reliably on the real pedal. The SysEx loop also tests
destination bits by active-array index, unlike the physical-port lookup in
the Program Change example. Non-contiguous MIDI-port configurations deserve
particular runtime verification. No SysEx was transmitted during this analysis.

### MIDI input: what software establishes, and where tracing stops

The included FlexSerial driver has generic RX setup at `0x4a22c` through
`0x4a302` and an RX interrupt branch in `0x49f2c`. But every configurable-port
serial construction in `0xae84` stores `0xffffffff` in its RX-pin word at
`+0x14`. Serial begin tests that word at `0x4a224` and skips RX setup when it
is `-1`. A generic receive implementation in the image therefore does **not**
establish a hidden configurable-port MIDI-input mode.

We now have the software-side assignments and mechanisms needed to write an
independent configurable-port MIDI-output driver: signal pairs, pin muxes,
peripheral bases, baud setup and interrupt handling. Remaining validation is
at the physical/runtime boundary:

- **Contact wiring and electrical interface:** continuity, series resistors,
  buffers, inversion, protection, actual voltage and the unused conductor's
  impedance require a schematic or board measurements. GPIO input capability
  does not establish a safe MIDI current-loop receiver. Do not connect another
  MIDI output as an experiment without establishing the input circuitry.
- **Stock behavior:** a MIDI capture from the unmodified pedal could check
  configurable-port SysEx and destination selection without custom firmware.
  This would be hardware validation, not another conclusion from disassembly.
- **Replacement-driver behavior:** actual output polarity, timing and startup
  transitions need measurement or an independently established, recoverable
  firmware-test route. No such route has been established here.

A test image could report mux and GPIO-direction registers over USB, reducing
the need for internal probing. It cannot prove external voltage tolerance,
isolation or continuity by reporting processor registers. Do not patch or flash
the production image just to answer those questions: bootloader acceptance and
recovery remain unresolved. Nothing was flashed, and no device I/O was attempted.

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

Independent bring-up: `firmware/usb_probe/displays.cpp` uses this wiring and
register data to draw `CNTRL` and a numbered display label at startup. Stock
rotation 1 resolves to MADCTL `0xa8`, with exchanged offsets x=1, y=2. All panel
CS lines are HIGH and clock LOW before button scanning begins. Reset and
sleep-out waits are conservative; PWM values 5 and 63 stay within traced stock
usage. Host SPI-transaction tests and the build pass; visible output and panel
positions remain unverified pending upload.

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
Configurable-port serial comparisons:
[FlexSerial](https://github.com/KurtE/FlexIO_t4/blob/master/src/FlexSerial.cpp)
and [FlexIO pin mapping and resource allocation](https://github.com/KurtE/FlexIO_t4/blob/master/src/FlexIO_t4.cpp).
These are comparison sources, not recovered Morningstar source or proof of an
exact core-library version.

This map is useful for a replacement driver, but is not yet a complete schematic
or evidence that flashing replacement firmware is recoverable. See the separate
[update and recovery trace](mc6-pro-recovery.md) for software bootloader entry,
flash layout and the remaining requirement for crash-independent recovery.
