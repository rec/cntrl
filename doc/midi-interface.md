# USB MIDI forwarding

Set `is_midi_interface = true` in the setup to enable MIDI forwarding on startup.
It defaults to `false`. The `usb-midi-interface` patch state operation can also
change forwarding. There is no separate MIDI Interface utility page; the
Out/In indicator matrix belongs to MIDI Cable Test and shows test streams only.

When enabled without a configured computer host, forwarding checks again every
two seconds until one is available. Losing the host starts the same retry cycle.
USB enumeration and command communication remain active independently of this
setting. MIDI received while waiting is not replayed after connection.

Forwarding uses the existing physical-to-USB and USB-to-physical routes without
USB re-enumeration. It generates no test messages and adds no physical MIDI thru
or USB-host routing. Disabling clears bridge queues and parser state, preserving
accepted pedal work and required SysEx termination.

MIDI Cable Test temporarily suspends forwarding while testing, then restores the
runtime setting when stopped or left. The setup is not edited by the tester.

The bridge owns forwarding and connection recovery. Target-specific qualification
is recorded in the [hardware investigation](../cntrl/maker/morningstar/mc/usb-midi-interface-plan.md)
and [target guide](../cntrl/maker/morningstar/mc/firmware/patch-switcher.md).
