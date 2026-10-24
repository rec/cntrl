# Standard USB MIDI interface

Status: implementation built and flashed, 2026-10-24. Automated tests and target
image checks pass. Firmware boots and setup copy/read-back works. Hardware
qualification is in progress: macOS currently retains the old single-port MIDI
layout, and the second interface has not appeared in USB/CoreMIDI discovery.
Do not treat enumeration or any physical MIDI path as qualified yet.

## Goal

Make the MC6 Pro a class-compliant USB MIDI 1.0 interface providing its two
physical MIDI inputs and six physical MIDI outputs to a computer. Keep patch
switching, browsing, editing, transitions, screensaver and USB configuration
working normally. No special computer application or driver is required for
ordinary MIDI use.

USB traffic must never wait for the computer, monopolize the application loop,
or block button scanning and display progress. Finite buffers cannot guarantee
lossless delivery under unlimited traffic. When capacity is exhausted, reject
or discard traffic explicitly and report it instead of blocking.

## Configuration and activation

Add one strict Boolean performance-setup field:

```toml
usb_midi_interface = false
```

It defaults to false, including when no setup is active. Carry it through setup
compilation, document validation, download and the pedal's setup editor. Follow
the existing document-version policy when changing the compiled format.

The switch controls forwarding, not USB enumeration. Keep the MIDI port list
and serial configuration interface stable throughout a connection. Selecting
a setup must not disconnect USB, recreate MIDI ports or interrupt a serial
configuration transfer. Disabled ports remain visible but do not bridge traffic.

When disabled, retain existing explicit pedal `usb-device` output behavior.
Drain unwanted USB MIDI input in bounded steps without forwarding it. Do not
configure unused configurable ports as outputs merely because they are
advertised over USB. Enabling the bridge claims all four configurable ports for
MIDI output, using the currently supported electrical mode and appropriate
TRS wiring. Expression and auxiliary-switch use on those ports is incompatible
with this mode; do not silently combine the roles.

On disabling or switching setups, stop accepting bridge traffic, discard its
pending queues and reset parser state. If a physical SysEx transmission has
already started, finish its abort procedure below before accepting ordinary
messages on that output. Never replay queued traffic after reconnecting USB or
reenabling the feature. Preserve accepted pedal-generated transactions and
their ordering; changing bridge state must not cancel those transactions.

## Computer-visible ports

Directions below are from the computer's perspective.

| Computer port | Physical connection |
| --- | --- |
| Input: DIN | DIN MIDI input |
| Input: TRS | Dedicated TRS MIDI input |
| Output: DIN | DIN MIDI output |
| Output: TRS | Dedicated TRS MIDI output |
| Output: Configurable 1 | Configurable port 1 |
| Output: Configurable 2 | Configurable port 2 |
| Output: Configurable 3 | Configurable port 3 |
| Output: Configurable 4 | Configurable port 4 |

Expose exactly two source jacks and six destination jacks in USB MIDI
descriptors, with stable names and cable numbering. Do not expose sixteen
dummy ports just because the library provides a sixteen-cable preset. Retain
CDC serial for `cntrl` configuration and software bootloader entry. Verify the
pinned core's descriptor and cable-validation assumptions before implementing
the asymmetric port counts. Keep necessary core changes small and reproducible
in the existing target build patch mechanism.

Keep the existing pedal `usb-device` route on its current cable zero, sharing
the first computer input. Consequently that input can contain both DIN input
traffic and explicitly USB-routed pedal messages. Document that behavior; do
not add an unrequested third input. Serialize those two producers safely,
including SysEx ownership, just as for a physical shared output.

No automatic physical MIDI thru, routing matrix, USB-host-port bridge or echo
back to the computer is included. Physical inputs go to their corresponding
computer inputs only. Computer outputs go to their corresponding physical
outputs only. Pedal-generated messages continue to use their configured routes.

## Current implementation and required changes

The target currently builds with `usb=serialmidi`, initializes Serial1 and
Serial2 at 31,250 baud, and uses four FlexSerial transmitters for configurable
ports. It drains USB MIDI input without forwarding it. Physical outputs and
pedal USB messages share a single 32-entry batch queue, which can cause
head-of-line blocking and must be replaced, not supplemented by a parallel
output path.

The pinned Teensy core's `usb_midi_write_packed()` can wait and call `yield()`
for up to 40 ms when transmit capacity is unavailable. Calling the ordinary
send API directly from the loop therefore does not meet this plan's contract.
Audit USB host and serial send paths too: a legacy USB route must not undermine
the responsiveness guarantee while this feature is enabled.

## Scheduling and buffering

1. Keep interrupt-driven UART reception and USB transfers enabled during
   display drawing. Interrupt handlers only move bounded data and update state;
   they do not parse documents, draw, access SD or log.
2. Scan buttons on the existing 3 ms schedule. Give every application iteration
   bounded MIDI receive/parse/transmit work, then bounded display work. Do not
   drain a source in an unbounded `while available` loop.
3. Use independent fixed-capacity queues and parser state for each route.
   Round-robin service prevents one saturated port starving another. Never use
   a global pending-output condition to suspend buttons or drawing.
4. Reserve capacity for accepted pedal transactions separately from forwarded
   USB traffic within each output scheduler. Accept a pedal patch batch
   atomically or report failure through the existing contract. Preserve batch
   order without permitting ordinary forwarded messages inside a patch batch.
5. Submit physical bytes only when driver transmit capacity is available.
   Introduce a genuinely non-waiting USB packet submission operation: it
   returns immediately with success or no capacity. A preliminary connection
   check alone is not sufficient. Do not call the blocking send function and
   hope the host keeps up. Preserve the core's transfer/flush behavior without
   waiting for a transfer to finish.
6. Allocate buffers statically. Record their byte totals and linker RAM impact.
   No full-message allocation for SysEx and no SD spooling of MIDI traffic.

Use initial MIDI service limits of at most 64 bytes/events per source per pass
and a 250 microsecond aggregate foreground MIDI budget per iteration. These
are engineering starting points, not measured guarantees; tune against full
two-input/six-output traffic while retaining the responsiveness targets below.

## MIDI correctness and sharing

Handle every MIDI 1.0 message class, all sixteen channels, system common,
real-time and streamed SysEx. Normalize serial running status into complete
messages before merging producers. USB packet encoding must preserve valid
message boundaries, cable numbers and SysEx end-packet lengths. Real-time bytes
may occur within other messages, including SysEx, without corrupting parser
state. Do not filter clock, active sensing or other valid messages implicitly.

A SysEx stream owns its destination until F7. Ordinary pedal messages cannot
interrupt it. Buttons and displays still respond immediately, but MIDI delivery
on that particular output can be delayed by the stream. Other outputs continue
independently. Give pending pedal batches priority at the next legal boundary;
do not allow a continuous stream of complete computer messages to starve them.

Use a one-second SysEx inactivity timeout, measured since the last SysEx data
or end byte accepted from its producer; unrelated real-time traffic does not
keep a broken stream alive. On timeout, disconnect, disable or overflow, discard
the remaining affected stream, enqueue one F7 if F0 was already transmitted,
then release ownership after that terminator is submitted. Do not block waiting
for F7 transmission. Consume and discard the rest of an overflowed source's
SysEx until its end or parser reset; never forward a truncated tail as a fresh
message. Count and report the abort. Apply the same ownership rules to shared
computer-bound cable zero.

For ordinary traffic, drop whole messages on overflow rather than partial
messages. Already transmitted notes can still become stuck when their note-off
is dropped; disclose this limitation. Do not invent automatic panic messages or
musical recovery policy in this feature. A computer can send faster than a
31.25 kbit/s physical output, so bounded loss under sustained overload is
unavoidable. USB bulk backpressure may help, but cannot be relied on to provide
independent flow control per virtual port.

## Diagnostics

Extend the existing status reporting with bridge enabled state, per-route
received/transmitted counts, dropped-message counts, aborted SysEx counts and
queue high-water marks. Read counters on request, not by unsolicited serial
logging or screen updates on every MIDI message. State clearly when an output
cannot be initialized. Diagnostics must not themselves create a blocking path.

## Implementation sequence

1. Add the default-off setup field and compiled representation, validate it,
   round-trip it through download, and expose its Boolean editor control.
2. Implement and test reusable MIDI parsing, packet conversion and bounded
   per-destination scheduling. Keep portable logic in the existing portable
   firmware area; keep all MC wiring, USB adaptation, target tests and hardware
   qualification notes under `cntrl/maker/morningstar/mc/`.
3. Replace the target's shared output queue with that scheduler, preserving
   current pedal route semantics and `output_idle()` transaction semantics.
4. Add exact port descriptors and non-waiting USB submission, then wire the
   two UART receivers and six transmitters into the scheduler.
5. Integrate setup transitions, reconnect handling and requested diagnostics.
6. Build and inspect the HEX, run automated tests, then qualify on hardware
   only when implementation and flashing are explicitly requested.

## Verification and acceptance

Native tests must cover message classes, running status, real-time interleaving,
SysEx spanning arbitrarily many chunks, cable mapping, shared-output ordering,
overflow, timeouts, disconnect and enable/disable transitions. Simulate a host
that never accepts another packet and saturated physical outputs. Prove that
each poll returns after bounded work, unaffected routes progress, and buttons
and rendering are still serviced. Check existing pedal batch and history tests
after replacing the queue. Add focused Python tests for default-off validation,
compilation and resolved-document round trips.

On the Mac, verify class-compliant discovery, correct names and exactly two
inputs/six outputs without a custom MIDI application. Confirm CDC setup copy,
status and software bootloader entry still work. Verify disabled mode produces
no bridge forwarding while explicit pedal USB output still works.

Using the second MIDI interface and loopback wiring, test every physical path,
simultaneous full-rate traffic, large SysEx, hot unplug/replug, missing listeners,
setup changes and repeated browsing/scrolling/screensaver transitions. Include
ordinary pedal messages sharing a busy computer-controlled output. Check counts
and byte sequences, not just audible results.

Measure added bridge latency and button-scan intervals under load. Initial
acceptance targets: no more than 1 ms added forwarding latency without output
congestion, no button scan gap above 5 ms attributable to MIDI work, and no
display freeze or starvation attributable to USB traffic. These are targets to
measure, not claims about the existing build. Existing SD-card stalls must be
distinguished from USB-induced stalls. Record queue sizes, peak occupancy,
RAM/flash usage and the tested traffic patterns before release. A stopped host
must cause bounded queuing/drops, never a 40 ms USB-send wait.

## Out of scope

No USB host bridging, arbitrary routing, MIDI 2.0, network MIDI, new expression
support, automatic panic, firmware self-update or unrelated graphics changes.
No implementation, runtime experiments or flashing in this planning change.

## Additional work beyond the prompt

None.

## Implementation notes and qualification record

The setup field is `usb_midi_interface`, strict Boolean, default false. The
compiled document is version 8: byte 19 bit 0 remains splash, bit 1 enables the
bridge, and other bits are rejected. Version 7 documents must be recompiled.
Before updating this pedal, its only named setup, `pman`, was downloaded to
`firmware/build/usb-midi-backup/pman.toml`. It was recompiled and copied back as
version 8 with byte-for-byte read-back verification. The backup and test outputs
are local ignored artifacts, not committed personal setup data.

Portable implementation: `firmware/include/cntrl/midi_bridge.h`. MC pin and USB
adaptation: `firmware/patch_switcher/target.cpp` in this directory. The target
build applies `firmware/usb-midi-interface.patch` to pinned Teensy 1.62.0. Both
USB speeds declare two computer inputs and six outputs with jack names. CDC is
retained. The USB device revision is increased to 0x0282 for the changed layout;
neither revision changes nor software-triggered reenumeration have refreshed
the MIDI layout observed on this Mac. Two bidirectional jack groups precede
the four output-only groups. A physical reconnect and host discovery check
remain pending; a cached host setup is a hypothesis, not an established cause.

Each scheduler destination has 256 four-byte forwarding packets and eight
reserved pedal batches, up to 14 bytes per batch. Total bridge storage is
11,168 bytes on the native build; verify the target's linker totals separately.
Pedal USB-host output is scheduler destination 8; it is not a bridged port.
Normal pedal USB-device output uses destination 6, computer input cable zero.
Bridge queues do not participate in `output_idle()`, so a stalled computer
cannot indefinitely prevent a setup change. Accepted pedal batches do.

Serial diagnostics, on request only:

```text
CNTRL MIDI STATUS N
CNTRL MIDI RESET
```

`N` is 0..8. One response contains:

```text
CNTRL MIDI enabled route received transmitted dropped aborted high_water max_midi_us max_scan_gap_ms scan_count display_count init_errors
```

Traffic counters count packets, not bytes; a dropped pedal batch on USB
disconnect counts as one dropped item. `display_count` counts display-service
calls, not completed frames. `init_errors` is a bitmask of failed configurable
port initializations. RESET clears the two timing maxima, not traffic counters
or queues. Measure after setup/SD operations to distinguish MIDI-induced waits
from existing SD stalls. Idle status reads show ongoing scans and display
service, but are not physical button/visible-rendering qualification.

The explicit macOS qualification tool is:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.qualify_usb_midi
```

It lists actual CoreMIDI endpoints. `--run --destination NAME --source NAME`
opens exactly those endpoints, checks short messages and a paced 4,099-byte
non-commercial SysEx, and prints per-route diagnostics. `--expect-enabled`
expects forwarding; without it the same messages must not arrive. Optional
`--stress-seconds N` floods only discovered cntrl outputs after the checked
messages, then reads diagnostics. During stress, dropped traffic is expected.
Do not equate an IAC-only validation of this tool with physical MIDI validation.

Automated results: 204 Python tests pass; portable MIDI/controller/persistence,
MC button/display/renderer and splash runtime tests pass. The MIDI tests include
all channel message types, running status, system common, realtime interleaving,
10 KB streamed SysEx, stalled destinations, queue reservation/overload, SysEx
timeout/abort/resynchronization, disconnect and default-off behavior. Target
build and stock flash-configuration hash verification pass. Physical input and
TRS/configurable output tests still require the corresponding loopback wiring.

Live results on the final bridge implementation before the descriptor-order
adjustment: an IAC-only check verified the qualification tool's 304 short-message
bytes and 4,099 SysEx bytes. This bypassed the pedal and is not a DIN result.
A separate five-second flood to the pedal's currently exposed USB output
received 511,056 new packets, transmitted 7,194 and dropped 503,862 under deliberate
overload. Queue occupancy stayed bounded at 256. After resetting timing maxima,
the maximum MIDI-service duration was 47 microseconds and button-scan gap was
3 ms; display-service calls continued. These counters establish scheduler
progress, not visible redraw quality or wire-level MIDI correctness.

The latest inspected/flashed image SHA-256 is
`0e9dd09363307bfdba2b5bb1bd78b93e11eb0c814be340f3b354df1fdd5726ad`.
After reset it reported `CNTRL READY 8 23390 e12bd7da`. The restored `pman` setup
has the bridge disabled, matching its original/default setting. Linker budgets:
RAM1 variables 324,352 bytes, code/padding 98,304, stack budget 101,632;
RAM2 fixed 215,072 bytes, heap budget 309,216; flash tool usage 395,260 bytes.
These are build budgets, not measured runtime stack/heap high-water marks.
