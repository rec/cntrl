# Incoming text messages

Send a cue or warning over MIDI SysEx or directly over USB serial. The first
display shows it for five seconds. A new complete message replaces it and
restarts the timer. Switches continue to operate, and expiry reveals the current
page. Text wraps to the display width and ends with an ellipsis when it exceeds
the available rows.

Both inputs accept 1–80 printable ASCII characters, including spaces. Empty
text, control characters and longer messages are rejected. Serial text works
with MIDI disabled. MIDI text requires its input transport to be enabled and
works with forwarding disabled. Monitoring, recording and configured forwarding
still see the received SysEx bytes.

## USB serial

Outside an active framed USB session, write either command as a plain line.
LF and CRLF are accepted. Text starts immediately after the space following
the kind, and spaces within the text are preserved.

```text
CNTRL TEXT CUE Chorus next
CNTRL TEXT WARNING Backing track stopped
```

The pedal replies with `CNTRL OK` on acceptance or `CNTRL ERROR text-message`
for invalid text or kind. A line exceeding the serial input buffer receives
`CNTRL ERROR command-too-long`. Replies end with LF. Wait for the reply before
sending the next line. Success acknowledges acceptance for display.

The same commands work through the existing framed USB command protocol. An
active framed session owns the connection, including during uploads. Close
that session before sending plain lines. Incomplete plain lines are discarded
on serial disconnect or after ten seconds without input.

## MIDI SysEx

Use this byte layout on physical MIDI or a USB MIDI cable. Numbers below are
hexadecimal. The identifier `7D` is for development use. The five ASCII bytes
`43 4E 54 52 4C` spell `CNTRL`, followed by version `01`.

```text
F0 7D 43 4E 54 52 4C 01 <kind> <ASCII text> F7
```

Kind `01` means cue and `02` means warning. A cue saying “Go” is

```text
F0 7D 43 4E 54 52 4C 01 01 47 6F F7
```

A warning saying “Stop” is

```text
F0 7D 43 4E 54 52 4C 01 02 53 74 6F 70 F7
```

Text becomes visible only when its complete valid frame arrives. Each input
and USB cable is assembled independently. Interleaved MIDI real-time bytes are
ignored by the text receiver. Malformed and interrupted frames leave the current
message in place, and a fresh SysEx start begins a new frame. SysEx text reception
sends no acknowledgement.

Automated tests cover decoding, USB session ownership and display restoration.
Physical display legibility and live MIDI/serial operation still require
hardware qualification.
