# USB MIDI interface page

MIDI Interface follows Panic at the end of the utility page cycle. It opens
with the setup's current bridge state and does not change routing on entry.
With a configured USB host, C temporarily toggles forwarding; F continues cycling
and restores the setup setting if it was changed. The setup itself is not edited
or saved.

Set `is_midi_interface = true` in the setup to enable MIDI forwarding
on startup. It defaults to `false`, leaving forwarding off until enabled with C.
If enabled without a configured host, forwarding checks again every two seconds
until one is available. Losing the host starts the same retry cycle. A manual
C enable with a host already present takes effect immediately. USB enumeration
and command communication remain active independently of this setting, and
MIDI received while waiting is not replayed after connection.

The setting replaces `usb_midi_interface`; update existing TOML sources to the
new name. Compiled setups retain their existing flag and need no conversion.

The page uses the shared background, heading and heavier text. A small toggle
icon in the bottom-right corner shows whether forwarding is enabled. The body
contains a two-by-two array of circular indicators, with DIN/TRS rows and
In/Out columns. Active indicators are bright green; inactive indicators are
grey. Without a configured USB host, the whole body including the switch is
greyed out, and C cannot toggle it. With a host and forwarding off, the switch
is available but the labels and indicators remain grey. With a host and
forwarding on, the whole body is enabled. The heading keeps its usual colors.
Disconnecting preserves the switch setting for reconnection. The other two
displays stay in place, and brightness is unchanged.

DIN in and TRS in indicate complete received MIDI packets, including real-time
and streamed SysEx, independently of onward queue acceptance. DIN out and TRS out
indicate successful physical transmitter submissions, rather than USB reception
or queue insertion. A lit output does not establish cable continuity or receipt
by an instrument. Each indicator remains lit for 100 ms after its latest event.
Off, exit and disconnection clear activity; reconnection does not replay it.

On enables the existing physical-to-USB and USB-to-physical routes without USB
re-enumeration. It generates no test messages and adds no physical MIDI thru or
USB-host routing. With no computer connected it remains responsive and does not retain physical
input for later replay. Off clears bridge
queues and parser state, preserving accepted pedal work and required SysEx
termination. Normal performance controls remain available on this page.

`firmware/include/cntrl/midi_interface.h` owns the switch, activity state and display-independent status snapshot.
The shared utility-page renderer supplies the layout.
`MidiInterfaceControl` supplies temporary bridge control and restores the setup
preference on exit. The target supplies host and activity events and optional
build inclusion. There is no dependency on the cable-test module or second
implementation of MIDI forwarding.

Native tests cover lifecycle, setting restoration, host state, independent
activity, expiry and timer rollover, stale-state clearing and disconnected-input
parsing. Target-specific qualification, deployment and remaining physical checks
are recorded in the [hardware investigation](https://github.com/rec/cntrl/blob/main/cntrl/maker/morningstar/mc/usb-midi-interface-plan.md)
and [target guide](https://github.com/rec/cntrl/blob/main/cntrl/maker/morningstar/mc/firmware/patch-switcher.md).
