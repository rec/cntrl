# USB keyboard input

Plug a standard USB keyboard directly into the pedal's USB host port. In the
song or setlist Rename editor, printable keys insert text at the current
character position. Backspace deletes the preceding character, Enter saves,
and Escape cancels the edit. The pedal's position buttons move the cursor;
the cursor starts at the first character. Names hold at most 63 characters.
Typing outside the Rename editor has no effect.

Input uses the firmware USB host driver's keyboard layout (US English in the
current build), including Shift and Caps Lock. Printable ASCII is supported;
Control, Alt and GUI/Command combinations are ignored. The existing pedal
button editor remains available. Physical keyboard enumeration and typing
still need to be checked on the pedal.

# USB keyboard output

Keyboard output is a patch action. Bind a press and matching release to a
switch to hold a computer shortcut for as long as the switch is held:

```toml
[switches.play]
button = 'A'
on = {operation = 'keyboard', action = 'press', keys = [44]}
gestures.release = {operation = 'keyboard', action = 'release', keys = [44]}
```

`44` is Space. Actions can also appear in sequences alongside MIDI messages
and delays. For a timed shortcut, sequence a press, a delay and a release.
Each action waits until its USB report is accepted before the sequence advances.

Keys are physical USB HID keyboard usages, from 4 through 115. Each action
contains one to six key usages. These usages describe keyboard positions.
The computer's keyboard layout and application's shortcut mapping determine
their meaning. Actions send held key reports rather than text.

Modifier bits, from least to most significant, are Left Control, Left Shift,
Left Alt, Left GUI/Command, Right Control, Right Shift, Right Alt and Right GUI.
Add their values to combine them. The default modifier mask is zero.

`release` must specify the same keys and modifier mask as its `press`.
Key order does not matter. Repeating a press of the same chord keeps a single
hold. Releasing one chord preserves keys and modifiers held by other chords.
Reports contain at most six distinct keys. A hold that would exceed that limit
is rejected as a whole.
A busy USB endpoint is retried by the next poll without blocking MIDI or the UI.

Panic, setup changes and stock-firmware handoff release all held keys. Browsing
pages leaves holds intact. Disconnecting USB clears all holds; reconnecting
first sends an empty report. Actions fail when USB is disconnected.

The target exposes a boot keyboard interface alongside USB serial and MIDI.
The compiler rejects keyboard actions on targets without keyboard capability.
Previously compiled setups containing the Keyboard page must be recompiled.
Remove that page and replace `page_settings.keyboard` shortcuts with patch actions
in the source setup first. Existing binaries are rejected rather than migrated.

Native tests cover patch execution, overlapping keys, release, USB backpressure,
disconnect, reconnect and report capacity. Setup serialization and export are checked
automatically. Physical enumeration and received key reports on macOS, Windows
and Linux remain to be qualified. The image is built for a coordinated flash.
