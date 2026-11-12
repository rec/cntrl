# Remaining live MIDI looper qualification

The looper implementation, Reverse edit, playback direction toggle and automated
qualification are complete. Configuration, controls, resource limits and
Save/Load behavior are in [the user guide](../doc/midi-looper.md).
Physical timing and receiver qualification remain outstanding.

## Physical timing and receiver qualification

Use an independently timestamped MIDI receiver to measure first-wrap gap,
long-run boundary drift, jitter and start/stop latency. Exercise notes and pedals
over up to 64 quarter-note beats, including a full 512-event take, while
scrolling, overdubbing and sending competing MIDI traffic.

Verify note/hold cleanup and boundary changes during overdub publication, Undo,
Reverse edits and forward/backward direction switches. Check tempo changes,
Start/Continue/Stop, clock loss, page navigation, setup replacement,
stock-firmware visits and port-owning diagnostics. A stopped committed phrase
must remain usable after cancellation or a failed candidate.

Exercise output backpressure, real disconnects and receiver replacement. Verify
that healthy destinations keep progressing, retired destinations receive only
required cleanup, and replacement devices never receive predecessor traffic.

## Physical storage qualification

Measure service latency during explicit Save/Load on real cards. Their
synchronous storage calls can introduce latency even though the first-wrap
handoff itself uses RAM2 without SD I/O. Exercise storage removal, failed writes
and power loss; existing saved files must survive and incomplete saves must not
be offered as completed recordings after reboot.

Record receiver captures, input-scan gaps, boundary lateness, RAM high-water
counts and practical capacity limits in the target's existing directory.
Host timing simulations and successful builds do not establish these physical
acceptance results.

## Additional work beyond the prompt

None.
