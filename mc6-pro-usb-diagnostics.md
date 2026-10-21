# MC6 Pro stock USB diagnostics

## Result

Static tracing on 2026-10-21 found USB-reachable hardware-test branches in the
stock application. It did **not** establish an arbitrary RAM, flash or
peripheral-register read command, a debugger shell or a general GPIO-control API.

The useful new lead is a standalone display test. Relay and EEPROM tests also
exist, but they are not read-only diagnostics and must not be tried as harmless
queries. No command in this report was transmitted to the pedal.

## Image and evidence

Analysed image:
`2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex`.
HEX SHA-256:
`228d73e768fad383bc205c8fc1ef52f3a49de08c754aca11d4a069ca1aab9fc8`.
Decoded binary SHA-256:
`1dbd4b59576c7468003e4bb09f3b842be6528344d9c3ba9d93e0cc0026f51307`.

All instruction addresses below are runtime ITCM addresses, using the mappings
in [the pin map](mc6-pro-pin-map.md#address-conventions-and-evidence).
Jump tables were decoded as data, not accepted as the spurious instructions
printed by linear disassembly. Indirect calls and runtime state limit the
strength of negative conclusions: "not found" does not mean proved absent.

## 1. The receive path reaches a shared application dispatcher

The normal receive pump at `0x1ba90` calls USB-MIDI decoder `0x4fc58` with
channel filter zero. For a completed SysEx message, `0x1bbc2` through `0x1bbde`
retrieve its buffer and length and pass dispatcher pointer **`0x2ec55`** to
wrapper **`0x1b9d0`**. Bit zero denotes a Thumb function pointer; the handler
itself starts at **`0x2ec54`**.

The wrapper checks manufacturer bytes `00 21 24` and model byte `00` or `06`.
It reads the received cable number at `0x2001345a`; cable indices 0 through 3
are passed to the common handler as transport selectors **0, 5, 6 and 7**
(`0x1ba30` through `0x1ba82`). Selector 4 belongs to USB serial, not MIDI
cable 1. These are application transport IDs, not physical connector numbers.
This is consistent with the earlier [four-cable experiment](mc6-pro-usb-experiments.md#four-cable-experiment),
although that experiment exercised only the public information query.

The common handler reads command bytes at message offsets 6 and 7. In this
report these are called **op1** and **op2**, respectively. The pair matters:
`00 32` is not the same operation as public query `70 32`.

At `0x2ecaa`, op1 `70` branches to the external-application API at `0x22484`.
Its decoded table at `0x22534` has cases:

```text
00 01 02 03 04 05 06 10 11 21 22 23 30 31 32
```

The other entries through `32` go to the common exit. Cases `21`, `22`, `23`,
`30`, `31` and `32` are fixed preset-name, bank-name, toggle and controller-info
queries. Case `06`, absent from the public case list used in the previous
session, updates preset fields and can save them when its save argument is
`7f`; it is not a diagnostic read (`0x22716` through `0x22772`).

The legacy/editor cases mostly sit behind a byte at controller-object offset
`+0x85` (`0x2ecb2` through `0x2ecce`). Its full state semantics were not
established here. The special op1 `00`, op2 `30` through `34` branches are
selected **before** that gate. Do not assume that leaving Editor mode makes
every legacy test command inert.

## 2. Hardware-test entry points

The halfword branch table at `0x2eca0` resolves as follows. These are command
selectors for analysis, **not validated packets or instructions to send them**.

| op1 / op2 | Branch target | Established effect |
| --- | --- | --- |
| `00 / 31` | `0x2f128` | Calls wrapper `0x276b4`, which starts dispatcher `0x2606c` with selector 0, reaching the EEPROM write/restore test |
| `00 / 32` | `0x2f110` | Calls standalone graphics test `0x18974` |
| `00 / 33` | `0x2ef08` | Calls dispatcher `0x2606c` with selector 4, the interactive relay test |
| `00 / 30`, `00 / 34` | `0x2ed34` | Shared UI/restart-related branch, not approved here as a diagnostic read |

The shared handler and surrounding UI state management mean these are not
isolated functions that can safely be invoked just by guessing a short packet.
Full packet construction, outer side effects and return-to-normal behaviour
still need review before a live test.

### Standalone display test: the most useful next candidate

`0x18974` loads a colour table from `0x200004bc`. It draws through the three
display-object pointers at `0x20010194`, `0x20010198` and `0x2001019c`.
The body includes full-screen fills and rectangular/line patterns. Its loop
increments the colour-table index modulo 14 and uses a roughly 150 ms interval.

It polls switch-container index **2**, corresponding to **C**, at `0x189a4`
and `0x18c28`. A true switch result exits via `0x18c32`, with a final 500 ms
delay. This is the physical switch reader, not execution of C's saved preset.

The direct calls in this graphics routine are switch polling and its final
delay; drawing occurs through display virtual methods. No EEPROM writer,
flash updater or MIDI-send call was identified in this routine itself.
That does not prove the surrounding command path has no other side effects.

This could validate the existing display-driver interpretation and physical
panel response without replacing firmware. It does not provide host-selected
register reads, arbitrary graphics commands or a framebuffer download.

### Relay test: exercises outputs, does not read their contacts

Selector 4 is chosen by `0x2ef1a` through `0x2ef1c`. In the dispatcher,
`0x260bc` through `0x260c0` select the relay path. It polls buttons and toggles
the already-mapped relay GPIOs, for example digital write at `0x26310` for
relay A tip. Switch index 2 exits through `0x26730`.

This is useful evidence that a stock test path uses those GPIO assignments.
Executing it could actuate connected equipment. Its status text describes the
commanded state; it does not measure whether a jack contact actually closed.
No host-readable contact-state or GPIO-register report was established.

### EEPROM test: explicitly unsafe as a read probe

Wrapper `0x276b4` immediately supplies selector **0** at `0x276b8`. The
dispatcher's non-1/2/3/4 branch reaches `0x271e2`, calling **`0x17010`**.

As already traced in [the EEPROM map](mc6-pro-pin-map.md#what-the-eeprom-test-actually-does),
this saves bytes 0 through 7, writes test values, verifies them and restores
the originals. A failure can return before restoration. It is not safe merely
because restoration is intended, and the All Banks JSON is not a byte-for-byte
backup of this storage.

The surrounding `00 / 31` branch also constructs a controller-restarting
message after the test. Do not use it to discover what USB diagnostics return.

## 3. USB serial is another SysEx transport, not an established shell

The same receive pump checks availability at **`0x505b0`** and reads bytes
through **`0x50638`**. Those functions use USB serial receive state and buffers;
the serial virtual-method thunks at `0x51bac` and `0x51bb0` point to them.

At `0x1bb22` the parser waits for `f0`, accumulates data bytes, and completes
on `f7` at `0x1bb3e`/`0x1bb6c`. It then calls **`0x11d98`**, which checks
manufacturer/model and invokes the registered common handler using transport
selector 4. Registration at `0x11dea` installs the common handler pointer in
`0x20011df4`.

This is a binary SysEx-framed path. No text console accepting address/read/write
commands was established. Opening a serial terminal and typing commands is
not justified by the presence of the CDC interface. In particular, baud 134
has the bootloader-entry side effect described in [the recovery report](mc6-pro-recovery.md#software-bootloader-entry-in-the-supplied-application).
No serial port was opened in this investigation.

## 4. Memory reads: fixed application data, not arbitrary addresses

There are real storage reads behind export commands. They must not be confused
with a CPU memory-inspection service. A concrete example is legacy `00 / 50`:

- Dispatch target `0x2f97a` calls **`0x21538`**.
- That routine supplies its own EEPROM start/end constants **`0x4aa`** and
  **`0x4cb`**, reading the half-open range, rather than taking an address from
  the incoming request (`0x21548` through `0x2159c`).
- Reads use the selected storage backend, with external EEPROM reader
  `0x3aaf8` or flash-emulation reader `0x4da10`.
- Bytes are masked with `0x7f` and emitted as an application-specific SysEx
  payload, not as a lossless arbitrary memory dump (`0x21572`, `0x215ea`).

Likewise, public `70 / 32` builds its nine-byte information reply from fixed
data at **`0x200004f4`** (`0x225d2` onward). The host cannot substitute a
GPIO or IOMUX register address for that source pointer.

No request-controlled address/length pair leading to arbitrary RAM, MMIO or
application-flash readback was found in the traced receive/dispatch paths.
The branch tables and selected callees were inspected; this is not an exhaustive
proof covering every indirect call, malformed-input behaviour or closed
bootloader implementation. No fuzzing or memory-corruption experiment was run.

## Practical consequence for independent hardware control

The stock application can run some of its own hardware tests without a new
image. That gives us possible behavioural checks, not a remote HAL capable of
reading pins, dumping mux registers or controlling arbitrary peripherals.
The tests are valuable primarily as executable references for implementing
our own drivers using the [existing pin/peripheral map](mc6-pro-pin-map.md).

The narrow next live candidate is the standalone display test, after reviewing
its surrounding handler and establishing normal-mode restoration. Relay and
EEPROM tests should remain unexecuted. There is no established software-only
raw-register probe to substitute for physical measurements, and these findings
do not improve recovery from firmware that cannot initialize USB.
