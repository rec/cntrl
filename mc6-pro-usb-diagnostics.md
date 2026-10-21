# MC6 Pro stock USB diagnostics

## Result

Static tracing on 2026-10-21 found USB-reachable hardware-test branches in the
stock application. It did **not** establish an arbitrary RAM, flash or
peripheral-register read command, a debugger shell or a general GPIO-control API.

The useful new lead is a standalone display test. Relay and EEPROM tests also
exist, but they are not read-only diagnostics and must not be tried as harmless
queries. The initial tracing did not transmit these commands. The subsequent
controlled display-test trial is recorded below.

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
The display-specific follow-up below resolves its entry and cleanup path;
the other commands remain untested.

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

## Display-test entry and return path

Further tracing resolves the main controller's virtual methods using the
initialized table at **`0x200026e0`**. The display command's full local path is:

| Stage | Call site | Resolved operation |
| --- | --- | --- |
| Entry | `0x2ec5e` | `0x1a8fc` temporarily clears normal DIN MIDI callbacks |
| Clear | `0x2f116`, virtual offset `+0x30` | `0xb18` clears all three display objects and RAM redraw flags |
| Flush | `0x2f120`, virtual offset `+0xd0` | `0xad4` calls each display's `+0x64` method |
| Test | `0x2f122` | `0x18974` draws the patterns and polls C |
| Exit delay | `0x18c32` through `0x18c3c` | Returns after a 500 ms delay once C is detected |
| Clear again | `0x2ef26`, virtual offset `+0x30` | `0xb18` clears the test graphics |
| Redraw | `0x2ef30`, virtual offset `+0x34` | `0x29950` receives redraw mask `0xf7` |
| Restore | `0x2ee2c` through `0x2ee70` | Reinstalls normal USB MIDI callbacks and registers the normal DIN/serial command handlers through `0x11dd4` |

The `0xf7` passed at `0x2ef2a` is a **UI redraw mask**, not an outgoing SysEx
terminator. The renderer at `0x29950` reads the current bank/preset and UI state,
rebuilds display content and restores preset indicator presentation. It sets
RAM redraw flags, including `+0xa5`, and returns to the receive pump. It does
not request a bank change or execute a preset as part of this path.

When controller flag `+0x85` is nonzero, the tail also updates a RAM timer at
`0x20012618`, then reaches the common exit through the default `00 / 32`
editor-table entry. This does not enter the adjacent `00 / 31` EEPROM test.

No EEPROM-write, flash-update, bootloader-entry or reset operation was identified
on this selected path. The display object's drawing/flush methods are the
existing stock drivers, not host-supplied code. This justified a one-shot test;
it is not a general safety claim about other factory commands.

C is switch-container index 2, registered with object `0x20012dc4` at
`0x3232c` through `0x32330`. Reader `0x4b070` calls the button's physical
input method, not its saved MIDI/navigation preset. The test does not service
normal incoming MIDI queries while looping. A queued information request is
therefore useful for detecting that control has returned afterward.

The test has no timeout-based exit in the traced loop. A user must operate C.
Tap and release it promptly: the 500 ms return delay provides time to release
before normal processing resumes. Holding it into normal operation could be
interpreted by the regular button handler; no promise is made that a held
button cannot subsequently navigate.

### One-shot USB trial

Before sending the display-test command, a fresh CoreMIDI inventory confirmed
the MC6 Pro's input/output ownership. Read-only queries returned the previously
observed firmware information and the current bank name. The received SysEx
checksums were verified, including a fragmented bank-name reply.

One command was then sent to the identified USB MIDI Port 1 output:

```text
f0 00 21 24 06 00 00 32 00 00 00 00 00 00 00 00 41 f7
```

The complete 18-byte frame selects `00 / 32`, not the neighbouring EEPROM or
relay operations. The final `41` is the XOR checksum masked to seven bits.
No bootloader request, serial-port open, configuration write or firmware upload
was performed by the host.

A single read-only `70 / 32` information query, transaction `43`, was queued
after the display command to detect the return to the receive pump. It received
no matching reply within 90 seconds, and the host script closed its connections.
It did not retry the display command or issue a reset. The planned post-test
bank-name query was consequently not sent.

At this point visual confirmation and C-exit confirmation were still pending.
The timeout is consistent with the blocking test loop, but does not independently
prove that graphics appeared or identify why no reply arrived. Successful return
to normal operation must not be claimed until a subsequent observation confirms
it. If the pedal is still displaying patterns, the traced exit is a brief C tap;
do not run the one-shot entry script again to check recovery.

The owner subsequently confirmed that the colour patterns appeared and that
tapping C returned the normal screen. This supplies the visual and physical
exit confirmation missing from the timed-out host check; the delay in responding
does not, by itself, establish the exact cause of the earlier USB timeout.

A separate post-exit check then sent only the previously tested read-only
`70 / 32` information query to each of the four USB MIDI outputs, with
transaction IDs `0a` through `0d`. All four received matching replies on Port 1
with the original information payload `06 03 0e 00 35 20 20 20 20`. All four
input connections reported zero dropped packets. The display-test entry command
was not repeated, and no reset or firmware upload was performed.

Together, the owner's observation and this host check confirm that the stock
display test ran, C exited it, and normal USB information-query processing
resumed. They do not prove that every saved setting is unchanged or that all
other controller functions have been tested; the post-test bank-name comparison
from the original script was not completed.

## Temporary preset-label text over USB

The public `70 / 01` command provides a different, non-flashing route to our
own displayed text: override a preset's short name in RAM. This uses the
existing preset-label layout, not arbitrary pixel positioning or a new graphics
driver. The dedicated `70 / 11` text-message case calls virtual offset `+0x48`,
which resolves to no-op `0xb48` in the main controller table inspected here;
its documented popup behaviour should not be assumed to work on this image.

The short-name handler at `0x227e8` requires a 32-byte payload. It selects the
preset, converts/copies that text and calls RAM setter `0x39b50` at `0x22810`.
It compares the save argument to `7f` at `0x2281c`. Only that value branches to
the separate save call at `0x22b26`; zero skips it and returns a success ACK.
The RAM setter updates the name buffer and dirty/presentation flags.

A controlled preset A trial used these steps:

1. Verify device ownership and the previously observed firmware-information
   payload, then read the current bank name and preset A's original 32 bytes.
2. Preserve the original bytes in a private local recovery snapshot outside
   this public repository.
3. Send `70 / 01`, preset index zero, save byte zero, and
   `HELLO FROM CNTRL` padded to 32 bytes with spaces.
4. Require a matching success ACK, then query `70 / 21` and compare the
   returned bytes exactly to the padded temporary text.
5. Leave the temporary label for 30 seconds, without button presses or bank
   changes, then restore the original bytes using the same save-disabled path.
6. Read the label back and require an exact byte-for-byte match to the original.

The original read returned `Flutes` padded to 32 bytes. The temporary write
received a success ACK and its readback exactly matched the test text.
After the 30-second interval, the bank-name read still matched. Restoration
received its own success ACK, and the final label read matched all 32 original
bytes exactly. The receiver reported zero dropped packets. All responses used
matching transaction IDs and passed checksum verification. Visual confirmation
is separate from this USB readback and was still pending when these results
were recorded.

The restoration step runs even if a trial write/readback fails. It checks that
the current bank name still matches before writing, to avoid knowingly
restoring into another bank. A bank-name match is not a unique bank identity;
the experiment also relies on the user leaving bank selection untouched.
No `7f` save flag, preset execution, reset or firmware-update command is sent.

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

The standalone display-test entry and cleanup have now been traced, and one
USB trial completed with owner-confirmed graphics/C-exit and successful
post-exit USB queries. Relay and EEPROM tests remain unexecuted. There is no
established software-only
raw-register probe to substitute for physical measurements, and these findings
do not improve recovery from firmware that cannot initialize USB.
