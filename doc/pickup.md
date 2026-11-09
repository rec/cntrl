# Live controller pickup

Pickup prevents a live controller from jumping a destination to its physical
position. The controller starts sending when its value reaches or crosses the
destination's last accepted value. Crossing works in either direction and does
not require an exact match between samples.

Set `pickup` on the setup or on an individual patch. The selected patch takes
precedence when its setting is not `None`; otherwise the setup setting applies.
If both settings are `None`, pickup is enabled. Use TOML booleans `true` and
`false`, or the strings `"none"` and `"None"` for inheritance. Omitting the field
also means inheritance. Python configuration accepts actual `None`.

```toml
pickup = true
pickup_pairing_ms = 2

[patches.direct]
patches = []
pickup = false

[patches.soft]
patches = []
pickup = "none"
```

Pickup applies to expression pedals and live MIDI CC, pitch bend, channel
pressure, polyphonic pressure, NRPN and RPN, after routing and transformations.
Each source, destination, MIDI channel and parameter acquires independently.
USB cable numbers distinguish sources. If the destination value is unknown,
the first value is allowed. Accepted changes from another source re-arm pickup.
Loading a setup or selecting a patch also re-arms it.
Disconnecting or replacing a MIDI input discards its unsent samples and re-arms
that source when it returns.

Patch messages, counters, generators, file playback and reversed playback
establish target values directly. Notes, bank changes, sustain and other MIDI
pedal switches (CC 64 through 69), data increment/decrement, channel-mode
messages and system messages pass through without pickup. Portamento note
selection (CC 84) and high-resolution note velocity prefixes (CC 88) also pass
through directly.

Paired CC values and NRPN/RPN data entry use all 14 bits when both components
arrive within `pickup_pairing_ms`. The default window is 2 milliseconds and the
setting accepts whole milliseconds from 0 through 65535. A lone component waits
for that window, then uses its 7-bit value. Setting the window to zero processes
components immediately. MIDI parameter selections stay associated with their
source, and the stage restores that selection before releasing parameter data.
An output that temporarily rejects a message retains the pending value; it does
not acquire pickup until delivery succeeds.

Targets reflect messages accepted by the pedal's MIDI output. Changes made
directly at an instrument remain unknown until a corresponding message is
accepted at that same destination.
