# Remaining incoming text message qualification

Incoming text messages and their automated checks are complete. Accepted SysEx
and USB serial formats, examples and display behavior are in
[the sender guide](../doc/text-messages.md).
Physical display and transport qualification remain outstanding.

## Physical qualification

- Send the documented cue and warning examples through physical MIDI, USB MIDI
  and USB serial. Verify the label and text on the active display, including
  wrapping, ellipsis and warning legibility on the smallest supported screen.
- With forwarding enabled, compare the forwarded MIDI bytes and monitor output
  with an independent receiver. Display messages must not consume or alter the
  original traffic. Also check serial text while MIDI is disabled.
- Alternate messages from the two transports. Verify replacement, the full
  five-second reading time and restoration of the current page after expiry,
  including patch or page changes made while the banner was visible.
- Continue using the switches under sustained MIDI traffic and existing binary
  transfers. Verify display responsiveness, transfer ownership and recovery
  after a physical disconnect or interrupted message.

Record sender bytes, receiver captures and display observations in the target's
existing directory. Host tests do not establish physical transport behavior or
on-pedal legibility.

## Additional work beyond the prompt

None.
