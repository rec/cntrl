# Incoming text messages

A computer should be able to send the pedal a cue such as “Chorus next” or
a warning such as “Backing track stopped”. The performer should see it on the
active display without opening the [MIDI monitor](../doc/midi-monitor.md).
This plan covers [Incoming text messages](feature-set.md#incoming-text-messages).

## Proposed sender and display behavior

- Use the same MIDI System Exclusive (SysEx) message on a physical MIDI input
  and on a USB MIDI cable. This lets a sequencer send either message directly.
- Also accept plain text over USB serial using newline-terminated commands
  `CNTRL TEXT CUE Chorus next` and
  `CNTRL TEXT WARNING Backing track stopped`. The text is everything after
  the space following the kind. Accept LF and CRLF using the existing serial
  command framing. Both inputs feed the same cue and warning display handling.
- Before implementation, agree on the SysEx identifier and command bytes with
  the sender. Define two message kinds, cue and warning. Start with printable
  ASCII and a maximum of 80 characters for both transports. Document the
  complete byte sequence for one example of each kind so a sender can reproduce it.
- Show a temporary banner on one visible display, with a “Cue” or “Warning”
  label readable in monochrome. Use the available width to wrap text and an
  ellipsis if the last line overflows. Use the first display on multi-display
  pedals. Confirm that placement and the proposed five-second reading time
  with the performer.
- The newest complete message replaces the current banner and restarts its
  timer. Footswitches keep their usual assignments. When the timer expires,
  redraw the current page, including any patch or page change made while
  the banner was visible.

## Firmware changes

1. Observe enabled incoming events in the [shared MIDI runtime](../firmware/include/cntrl/midi_runtime.h)
   before routing and forwarding. Read the valid bytes in each
   [packet](../firmware/include/cntrl/midi_packet.h). A display message can
   span many three-byte packets. Keep bounded decoder state per input endpoint
   and USB cable so simultaneous senders cannot combine into a single message.
2. Publish text only after receiving the terminator of a valid frame. Ignore
   interleaved real-time bytes when assembling it. Discard empty, oversized,
   malformed or interrupted frames without changing the visible banner.
   Restart on a fresh SysEx start byte and clear partial input on disconnect.
   Observe packets without consuming them, so the monitor and configured
   forwarding and recording paths still receive the original traffic.
3. Extend the existing [USB serial command router](../firmware/include/cntrl/animation/router.h)
   to recognize the two text commands. Validate kind, printable text and the
   shared 80-character limit before publishing. Reject empty or malformed
   commands using the existing serial error reply convention. Keep partial
   lines bounded and use the existing overflow and timeout handling. Respect
   ownership of active binary transfers so their payload cannot be interpreted
   as text commands. Serial text works independently of MIDI enable settings.
4. Hand completed messages from either transport to the UI through the existing
   runtime-to-display path. Keep only the visible message, kind and expiry. Render on the normal
   display refresh cycle and check expiry without blocking MIDI service.

## Verification

- Feed the documented cue and warning bytes through the firmware input path
  with forwarding disabled. Check the displayed label and text for both
  physical MIDI and USB MIDI input. With forwarding enabled, also check
  that the forwarded bytes and monitor observations are unchanged.
- Split a frame at packet boundaries, interleave two inputs and insert MIDI
  clock bytes mid-frame. Verify exact text and one display update per complete
  frame. Exercise unrelated SysEx, invalid kind, overlong text, interrupted
  input and recovery with the next valid frame.
- Send both documented serial commands with LF and CRLF, split across reads
  and back-to-back. Verify text and kind, rejection of invalid and oversized
  commands, and recovery after overflow, timeout or disconnect. Check serial
  text with MIDI disabled and existing commands and binary transfers with
  text support enabled.
- Alternate complete serial and SysEx messages. Verify that the most recently
  completed message becomes visible and restarts the same banner timer.
- Test replacement just before expiry and a page change during a banner.
  Check that the second message gets its full reading time and expiry reveals
  the newly selected page. On hardware, check wrapping and warning legibility
  on the smallest supported screen while continuing to operate the switches.

## Additional work beyond the prompt

None.
