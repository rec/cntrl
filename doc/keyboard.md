# Keyboard page

Keyboard follows Clock Tempo in the page cycle. A and B select a
shortcut. Hold C to hold its keys and release C to release them. Selecting another
shortcut, leaving the page or changing setups releases held keys. A disconnected
USB connection clears all holds. Reconnecting first sends an empty report.

Configure shortcuts with the setup. The list uses one record per shortcut,
up to the target’s total setup capacity:

```toml
[[page_settings.keyboard]]
name = 'Play'
keys = [44] # USB HID keyboard usage for Space

[[page_settings.keyboard]]
name = 'Record'
keys = [21] # Physical R key on a US layout
modifiers = 9 # Left Control (1) plus Left GUI/Command (8)
```

Keys are physical USB HID keyboard usages, from 4 through 115. Each shortcut
contains one to six key usages. These usages describe keyboard positions.
The computer's keyboard layout and application's shortcut mapping determine
their meaning. This page sends held key reports rather than text.

Modifier bits, from least to most significant, are Left Control, Left Shift,
Left Alt, Left GUI/Command, Right Control, Right Shift, Right Alt and Right GUI.
Add their values to combine them. The default modifier mask is zero.

The shared report owner tracks each hold independently. Releasing one owner
preserves keys and modifiers held by another. Reports contain at most six
distinct keys. A hold that would exceed that limit is rejected as a whole.
A busy USB endpoint is retried by the next poll without blocking MIDI or the UI.

The target exposes a boot keyboard interface alongside USB serial and MIDI.
The compiler rejects keyboard configuration on targets without keyboard
capability. Empty configuration displays “No shortcuts”.

Native tests cover overlapping keys, release, USB backpressure, disconnect,
reconnect and report capacity. Setup serialization and export are checked
automatically. Physical enumeration and received key reports on macOS, Windows
and Linux remain to be qualified. The image is built for a coordinated flash.
