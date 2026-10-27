# MIDI cable test

The cable test is an optional active-tool module. Opening its page always starts
with the switch Off and both result lines empty. C toggles On/Off. F stops the
test and returns to the module menu. Leaving and reopening never restores On.

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

`firmware/include/cntrl/cable_test.h` owns the test state, message generation,
return matching and page. It depends on the shared hardware interface and MIDI
parser. The target owns port access, actual transmission, input delivery and
module navigation. No module calls another module.

The current target waits for queued performance sends to drain before granting
access. While the tester page is open, the normal USB MIDI bridge is suspended;
USB input is discarded and physical input is delivered to the tester without
forwarding. Test messages go directly to the bounded physical output buffers,
so no test backlog survives switching Off or leaving. Bytes already handed to
the UART finish transmitting. Leaving restores the setup's bridge setting.

The initial selector lists installed tools explicitly. It does not introduce
runtime discovery, dynamic plugins or a new setup configuration format.

## Verification

Portable native tests cover Off on entry, channel/controller/output selection,
all four labels, expiry, unrelated messages, running status, failed sends,
re-entry and timestamp wraparound. Firmware builds must succeed with the tester
included and excluded. Physical loopback and display behavior require separate
pedal verification.
