# MIDI cable test

The cable tester is the utility page after Firmware and before Panic. Opening it starts
Off with both result rows empty. C toggles On/Off. F stops any active test and
continues the page cycle. Re-entering never restores On.

The page uses the shared configurable background, white-on-black heading, and
heavier text without wider spacing. DIN and TRS results occupy fixed middle rows.
The bottom-right toggle uses the same icon and position as MIDI Interface and
shows whether testing is enabled. C operates the toggle. The other two displays
remain in place.

While On, the module sends a stream of breath-controller messages on both test
outputs. [MIDI CC 2 is Breath Controller](https://midi.org/midi-1-0-control-change-messages).
DIN uses channel 1; TRS uses channel 2. Each output sends one message every 10 ms,
with values cycling through 0..127. A late poll sends at most one message per
output, without a catch-up burst. A blocked output does not block the other.

The two result lines correspond to the transmitting outputs:

| Output | Return input | Result |
| --- | --- | --- |
| DIN | DIN | `DIN` on the first line |
| DIN | TRS | `DIN to TRS` on the first line |
| TRS | TRS | `TRS` on the second line |
| TRS | DIN | `TRS to DIN` on the second line |

Only complete CC 2 messages on the appropriate channel, with a recently sent
value, count as returns. The receiver handles running status and interleaved
real-time messages using the shared MIDI parser. Each output retains its last
eight accepted test values. A result disappears after 250 ms without a matching
return. Switching Off clears both results immediately.

This checks delivery of MIDI messages, not individual connector-pin continuity.

## Boundaries

`firmware/include/cntrl/cable_test.h` owns message generation, return matching
and a display-independent status snapshot. It uses the shared MIDI sink and
parser. The target owns port access and navigation; the shared utility-page
renderer supplies the layout.

Browsing the page while Off leaves normal MIDI operation unchanged. Starting a
test requires pending performance sends and setup transfers to finish. While On,
the normal USB MIDI bridge is suspended, USB input is discarded, and physical
input goes to the tester without forwarding. Performance buttons are inactive;
C stops testing and F leaves the page. Test messages go directly to bounded
physical output buffers, so no test backlog survives stopping. Bytes already
handed to the UART finish transmitting. Stopping restores the setup's bridge
setting. Leaving performance mode also stops the test.

## Verification

Portable native tests cover Off on entry, channel/controller/output selection,
all four labels, expiry, unrelated messages, running status, failed sends,
re-entry and timestamp wraparound. Firmware builds must succeed with the tester
included and excluded. Physical loopback and display behavior require separate
pedal verification.
