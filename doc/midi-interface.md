# USB MIDI interface module

Open MIDI interface from the module menu. Each visit starts Off. B toggles
forwarding; F returns to the module menu and restores the setup's active bridge
preference. Toggling the tool does not save or modify that preference. The module
uses the existing bridge, parsers, queues and output scheduling.

The center display shows the title, host connection, four activity indicators
and On/Off above B. The outer displays are black except for the right-hand Back
label near F. Backlighting stays unchanged. While Off, activity is cleared and
the host-status value is hidden. While On, Host connected means the USB device
has been configured by the computer. It does not mean a DAW opened a MIDI port.

DIN in and TRS in indicate complete received MIDI packets, including real-time
and streamed SysEx, independently of onward queue acceptance. DIN out and TRS out
indicate successful physical transmitter submissions, rather than USB reception
or queue insertion. A lit output does not establish cable continuity or receipt
by an instrument. Each indicator remains lit for 100 ms after its latest event.
Off, exit and disconnection clear activity; reconnection does not replay it.

On enables the existing physical-to-USB and USB-to-physical routes without USB
re-enumeration. It generates no test messages and adds no physical MIDI thru or
USB-host routing. With no computer connected it remains responsive, reports No
host, and does not retain physical input for later replay. Off clears bridge
queues and parser state, preserving accepted pedal work and required SysEx
termination. The target coordinates exclusive tool ownership.

`firmware/include/cntrl/midi_interface.h` owns the page, switch and activity state.
`MidiInterfaceControl` supplies temporary bridge control and restores the setup
preference on exit. The target supplies host and activity events and optional
build inclusion. There is no dependency on the cable-test module or second
implementation of MIDI forwarding.

Native tests cover lifecycle, setting restoration, host state, independent
activity, expiry and timer rollover, stale-state clearing and disconnected-input
parsing. Target-specific qualification, deployment and remaining physical checks
are recorded in the [hardware investigation](../cntrl/maker/morningstar/mc/usb-midi-interface-plan.md)
and [target guide](../cntrl/maker/morningstar/mc/firmware/patch-switcher.md).
