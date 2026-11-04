# USB MIDI forwarding

Both USB roles connect automatically. These independent setup values default to
true:

```toml
usb_device_midi = true
usb_host_midi = true
```

`usb_device_midi` enables MIDI through the USB Device port connected to a computer,
phone or tablet. `usb_host_midi` enables MIDI through the USB Host port connected
to a supported USB MIDI peripheral. The host currently supports one MIDI device.
Disabling either setting blocks MIDI input and output on that port and cancels
its queued output. Re-enabling does not replay old traffic. Incoming MIDI clock
from a disabled port is ignored.

These settings gate MIDI communication, not USB power or enumeration. USB serial
commands, setup transfers and firmware-update requests remain available even
when USB Device MIDI is disabled. Connection and reconnection need no manual
action when the corresponding MIDI setting is enabled.

Setup format 28 stores these flags. Recompile older setups before using them
with the new firmware.

Set `is_midi_interface = true` in the setup to enable MIDI forwarding on startup.
It defaults to `false`. The `usb-midi-interface` patch state operation can also
change forwarding. There is no separate MIDI Interface utility page; the
Out/In indicator matrix belongs to MIDI Cable Test and shows test streams only.

When enabled without a configured computer host, forwarding checks again every
two seconds until one is available. Losing the host starts the same retry cycle.
Forwarding also requires `usb_device_midi = true`; the USB Host setting does not
enable additional forwarding routes.
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
