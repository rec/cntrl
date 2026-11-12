# Remaining MIDI monitor qualification

The monitor implementation and automated checks are complete. Configuration,
layouts, controls and observation semantics are in
[the user guide](../doc/midi-monitor.md).
Physical timing and display qualification remain outstanding.

## Physical qualification

- Measure MIDI service time, input-scan gaps and clock jitter with the monitor
  open and closed under simultaneous DIN/TRS and USB traffic, sustained Clock
  and long SysEx. Check actual MIDI delivery with an independent receiver.
- Check readability, direction markers, chronological ordering and refresh rate
  on the actual displays for the one-, two- and three-window layouts. Verify
  that remaining performance displays and background patch execution continue
  while monitoring.
- Exercise Freeze, history browsing and overflow during live traffic. Check
  that switching pages or setups does not alter musical state, and that monitor
  overflow is distinguishable from actual MIDI delivery failures.
- Verify disconnect and replacement behavior on enabled physical transports.
  Partial input must not combine with bytes from a replacement device.
- Compare incoming and outgoing rows with receiver captures during routing,
  fan-out, generated MIDI, cleanup, panic and cable diagnostics. An outgoing row
  means local transport acceptance, not acknowledgement from an instrument.

Record target-specific captures, timing and display observations in the target's
existing directory. Automated tests do not establish wire timing or physical
display responsiveness.

## Additional work beyond the prompt

None.
