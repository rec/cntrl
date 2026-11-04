# USB MIDI interface page

MIDI Interface follows Panic at the end of the right-hand page cycle. It opens
with the setup's current bridge state and does not change routing on entry.
C temporarily toggles forwarding; F continues cycling and restores the setup
setting if it was changed. The setup itself is not edited or saved.

The page uses the shared background, heading and heavier text. It shows host
connection, four activity indicators and On/Off above C. The other two displays
stay in place, and brightness is unchanged. Host connected means the USB device
has been configured by the computer, not that a DAW opened a MIDI port.

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
termination. Normal performance controls remain available on this page.

`firmware/include/cntrl/midi_interface.h` owns the switch, activity state and display-independent status snapshot.
The shared right-page renderer supplies the layout.
`MidiInterfaceControl` supplies temporary bridge control and restores the setup
preference on exit. The target supplies host and activity events and optional
build inclusion. There is no dependency on the cable-test module or second
implementation of MIDI forwarding.

Native tests cover lifecycle, setting restoration, host state, independent
activity, expiry and timer rollover, stale-state clearing and disconnected-input
parsing. Target-specific qualification, deployment and remaining physical checks
are recorded in the [hardware investigation](../cntrl/maker/morningstar/mc/usb-midi-interface-plan.md)
and [target guide](../cntrl/maker/morningstar/mc/firmware/patch-switcher.md).
