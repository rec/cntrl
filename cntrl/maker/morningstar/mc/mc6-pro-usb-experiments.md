# MC6 Pro USB experiments

## Session: 2026-10-21

These are live observations from the photographed MC6 Pro, after its owner
reported installing the application image analysed in this repository and
exporting an All Banks backup. They are not observations of a replacement OS.

Only read-only inventory and documented information queries were performed.
No reset, bootloader entry, serial-port open, flash operation, bank change,
preset execution, configuration write or restore was requested.

## USB and MIDI inventory

macOS IORegistry identifies the running device as:

| Property | Observed value |
| --- | --- |
| Product | Morningstar MC6 Pro |
| Manufacturer | Morningstar Engineering |
| USB vendor/product IDs | `331b:0007` |
| USB device release field | `0x0280`, not an application version query |
| USB configuration count | 1 |

The device exposes CDC serial interfaces as well as MIDI. The serial interface
was not opened, particularly because the application's 134-baud handling is
part of the bootloader-entry mechanism described in
[the recovery report](mc6-pro-recovery.md).

CoreMIDI reports four inputs and four outputs, named `Port 1` through `Port 4`.
Their short names do not identify the manufacturer. A separate read-only
CoreMIDI property query established that **all eight endpoints belong to the
Morningstar MC6 Pro** before any messages were sent. The unrelated IAC Bus 1
was neither connected nor addressed.

Inventory and transport used the existing sibling `meedee` environment through
standalone `uv run --frozen` invocations. No project dependency was added.
The first inventory invocation rebuilt the existing local editable meedee
package. Experimental clients were temporary files outside this repository.

## Read-only queries on USB MIDI Port 1

Requests follow Morningstar's
[SysEx documentation for external applications](https://helpdesk.morningstar.io/support/solutions/articles/43000681034-sysex-documentation-for-external-applications).
The MC6 Pro model ID is `06`; the external-application opcode is `70`.
All values below are hexadecimal. Each request used a different transaction ID.

| Opcode pair | Requested information | Live result |
| --- | --- | --- |
| `70 32` | Controller information | Model `06`, version bytes `03 0e 00 35`, 32 messages per preset, three reported name sizes of 32 |
| `70 30` | Current bank name | 32-byte padded name returned |
| `70 21`, preset index 0 | Preset A short name | 32-byte padded name returned |
| `70 31` | Current-bank preset toggle states | 24 payload bytes, all zero in this observation |

The version bytes report major/minor/patch `3.14.0`, with fourth byte `0x35`
(decimal 53). This is not a hash or proof that the installed flash is identical
to the supplied HEX file. Names are omitted here because they are user data.

Example controller-information exchange, transaction `01`:

```text
TX Port 1:
f0 00 21 24 06 00 70 32 00 00 00 00 00 01 00 00 30 f7

RX Port 1:
f0 00 21 24 06 04 70 32 00 00 00 00 00 01 00 1b
06 03 0e 00 35 20 20 20 20 11 f7
```

The reply differs from the documentation's example header: byte 5 is `04`,
byte 9 is zero for the controller-information reply, and byte 15 is `1b`,
equal to this reply's total 27-byte length. Do not assume every reply follows
the documented payload-size field literally.

The bank-name reply also retains opcode `30`, whereas the documentation's
bank-name response example shows `21`.

## Four-cable experiment

All four inputs were monitored simultaneously while one `70 32` request was
sent to each output in turn, using transaction IDs `0a` through `0d`.

| Request destination | Matching response source |
| --- | --- |
| USB MIDI Port 1 | USB MIDI Port 1 |
| USB MIDI Port 2 | USB MIDI Port 1 |
| USB MIDI Port 3 | USB MIDI Port 1 |
| USB MIDI Port 4 | USB MIDI Port 1 |

Each request received one matching reply during its two-second observation
window. No other input delivered a packet in those windows. Both experimental
runs reported zero dropped packets on all four input connections. Each run's
initial two-second passive baseline was silent.

**Consequence:** this stock application accepts the tested information query
on all four USB MIDI receive cables, but sends its replies on cable 1. A client
listening only to the cable it transmitted on can miss valid replies.

This does not establish forwarding rules, configurable-port routing or the
behaviour of other command families. Silence during a short baseline does not
prove that a port never emits unsolicited traffic.

## Framing observation

Both 50-byte name replies arrived as two raw MIDI callbacks: 48 bytes followed
by the checksum and `f7`. The 27-byte controller-information and 42-byte toggle
replies each arrived in one callback in this session.

Receivers must reconstruct SysEx messages through the terminating `f7`; one
transport callback is not necessarily one complete message. The observed
48-byte division does not establish a universal transport limit.

## Passive footswitch capture

A subsequent 45-second capture listened to all four MC6 Pro inputs. It opened
no MIDI output, sent no query, and did not open the serial interface. The user
was asked to press and release A through F once, in order, and reported done.
Expression pedals were excluded at the user's request.

Five raw callbacks arrived, all on Port 1. Each contained two complete Program
Change messages, on MIDI channels 1 and 2 respectively. Program values in the
table are the zero-based values actually transmitted, not 1-based display
numbers.

| Seconds from start | Raw bytes | Channel 1 program | Channel 2 program |
| --- | --- | --- | --- |
| 7.588721 | `c0 2f c1 15` | 47 | 21 |
| 10.771429 | `c0 39 c1 1a` | 57 | 26 |
| 16.427461 | `c0 06 c1 06` | 6 | 6 |
| 19.342734 | `c0 0b c1 0b` | 11 | 11 |
| 22.942897 | `c0 10 c1 10` | 16 | 16 |

Ports 2 through 4 delivered no packets. All four connections reported zero
dropped packets. No SysEx, Control Change, note or explicit button-edge message
was observed in this window. The capture itself was passive, but the user's
button presses still executed their configured presets normally.

Do not assign these five bursts to A through F by position. There are fewer
bursts than requested presses, and no independent timestamped button markers.
The saved backup does not establish the live bank/page or mutable preset state
at each press. Inspecting its stored message records did not establish a
reliable correspondence to the five captured program pairs.

Neither the missing sixth burst nor the absence of release-specific messages
proves a hardware fault. A preset may send nothing over USB, perform an internal
action, use another destination or depend on runtime state. This experiment
does not distinguish those possibilities.

The original raw callback log, including host timestamps, was saved locally at
`/private/tmp/mc6-pro-passive-20261021T120859Z.jsonl`; that temporary file is not
a durable archive. The complete received byte sequences are preserved above.

This also demonstrates the opposite framing case to the split SysEx replies:
one raw callback can contain **multiple complete MIDI messages**. A decoder
must handle both aggregation and fragmentation rather than count callbacks as
messages or switch actions.

## What this establishes, and what remains

Follow-up static tracing found stock USB hardware-test entry points, including
a standalone display test, but no established arbitrary memory-read command.
See [stock USB diagnostics](mc6-pro-usb-diagnostics.md) for the command paths,
side effects and limits. None of those newly traced commands was transmitted.

We have a working, non-flashing path for querying the stock firmware directly,
without controlling the Editor. Transaction IDs survive in replies, and the
four-cable response behaviour is now observed rather than inferred.

These queries do not expose raw RAM, peripheral registers, flash readback or
an executable upload facility. They do not demonstrate recovery from broken
firmware. At this session's date the recovery gate remained unresolved; later
PROGRAM and software-entry tests are recorded in the
[updated recovery report](mc6-pro-recovery.md).

Passive footswitch capture now works without changing controller settings.
A labelled, one-button-at-a-time capture would resolve which switches produced
which output and whether a particular press is silent. Expression-pedal testing
is outside the current scope. Relating configurable-port signals to external
wiring still requires a connected peer or measurement; the USB information
queries and passive capture alone cannot establish electrical behaviour.
