# USB MIDI interface module

Status: implemented as a standalone module using the existing USB MIDI bridge.
Not flashed; physical module verification remains pending.

## Goal and existing foundation

Provide an optional module with an On/Off switch for the standard USB MIDI
interface, host connection status and four physical-port activity indicators.
Reuse the existing bridge, USB descriptors, parsers, queues and output scheduling.

The [existing bridge investigation and qualification record](../cntrl/maker/morningstar/mc/usb-midi-interface-plan.md)
documents successful operation with the development computer, including physical
DIN output checks with short messages and streamed SysEx. Reuse that evidence
without claiming that every physical direction has already been qualified.
Follow its current scope: DIN and dedicated TRS input/output only. Unused
advertised configurable outputs remain unused.

## Controls and lifecycle

Add `MIDI interface` to the installed module menu. Use the cable-test controls:

- B toggles On/Off.
- F goes Back to the module menu.
- Every visit starts Off.

While the page is open, its switch temporarily controls bridge forwarding. On
entry, claim interface control and disable forwarding even if the performance
setup had it enabled. On exit, release control and restore the active setup's
existing interface setting, as the cable tester does. Do not modify or save that
setting when toggling the module. There is one bridge with one effective enable
state, not a separate module implementation of the interface.

Switching Off immediately stops accepting new bridge traffic and clears pending
bridge queues and parser state using the existing disable behavior. Preserve
the existing rules for terminating interrupted SysEx and accepted pedal sends.
No queued bridge traffic is replayed on re-enabling or reconnecting.

Switching On enables forwarding without disconnecting USB or rebuilding its
port list. No test messages are generated. With no host connected, the module
can remain On and wait for a connection without blocking the UI.

## Display

Use the centered cable-test presentation: black backgrounds, unchanged
backlighting, and a white `Back` label at the top-right of the right screen near
F. The left screen stays black. The center screen contains:

```text
         MIDI interface
     Host connected / No host
     DIN in         TRS in
     DIN out        TRS out
            On / Off
```

Each port label has its own activity light. Keep labels, lights, host status and
switch in fixed positions so activity never moves other content. Center On/Off
above B. The title may use the same bundled electric-style font as the cable
tester; the status and port labels use the ordinary readable font.

While Off, retain the title, labels and Off indicator, leave all activity lights
unlit, and hide the host-status value. While On, show current host status and
update the four lights independently.

### Host status

`Host connected` means the USB device is configured by the host, using the
existing USB connection state. `No host` means it is not configured. This reports
the connection to the computer, independently of whether MIDI traffic is flowing.

Do not label this as a DAW connection or claim that a particular application has
opened a MIDI port. Standard USB MIDI does not provide that application-level
notification. An idle configured host remains connected; activity is not a
substitute for connection status.

### Activity lights

| Light | Activity that lights it |
| --- | --- |
| DIN in | A complete MIDI packet received on physical DIN input |
| TRS in | A complete MIDI packet received on physical TRS input |
| DIN out | A MIDI packet successfully submitted to the physical DIN transmitter |
| TRS out | A MIDI packet successfully submitted to the physical TRS transmitter |

Include channel messages, real-time messages and streamed SysEx packets. Input
lights reflect physical reception even if onward USB delivery is blocked or a
queue is full. Output lights reflect successful transmitter submission, not
mere arrival from USB or insertion into a queue. A lit output does not prove
that a connected cable or receiving device works.

Keep each light on for 100 ms after its most recent activity, so isolated
messages are visible and a continuous stream appears steadily lit. Use a
monotonic timestamp per port; handle timestamp wraparound. Clear all activity
state on entry, Off, exit or host disconnection. Reconnection cannot revive old
activity. Update the display only when visible status changes.

## Module boundaries and implementation

The module owns its switch state, activity timestamps and page. The core owns
USB connection state, forwarding, physical ports and output scheduling. Deliver
connection changes and physical activity to the module through small core
interfaces. Reuse existing counters or parser/transmitter completion points;
do not infer physical activity solely from a USB receive counter.

Keep input observation independent of bridge queue acceptance. Do not add a
second parser or duplicate routing logic to obtain indicator events. Enabling
the module must preserve the existing physical-to-USB and USB-to-physical routes,
without adding physical MIDI thru, loopback or USB-host routing.

The cable tester and interface page cannot own the physical paths concurrently.
The module selector deactivates the previous tool before activating this one.
Keep work cooperative and bounded so sustained MIDI traffic cannot stall button
scanning, serial configuration or display progress.

Include the module through the explicit static module list, with a build option
to exclude its code and menu entry. No runtime plugin system or new persisted
setup field is needed.

## Verification

- Entry is Off; B toggles; F releases control; saved setup settings stay intact.
- Enabling and disabling do not change USB enumeration or interrupt serial access.
- Host configured/disconnected transitions display correctly without requiring
  MIDI traffic, including an enabled page waiting for a host.
- Each physical path lights only its own indicator, with independently refreshed
  100 ms pulses, continuous activity and correct timer wraparound.
- Input activity remains visible under USB backpressure; failed or queued output
  does not light a transmitter indicator prematurely.
- Off, exit and disconnect clear stale lights and pending bridge traffic.
- Switching between the cable tester and interface page releases port ownership
  and never forwards test traffic through the bridge.
- Native logic tests and included/excluded firmware builds pass. Repeat physical
  DIN/TRS input and output checks on the development computer, including short
  messages and SysEx. Verify the four lights and readability on the pedal.

## Additional work beyond the prompt

None. Implementation is limited to this module and its core integration.

## Implementation

`firmware/include/cntrl/midi_interface.h` owns the page, enable state, host status
and activity pulses. Its `MidiInterfaceControl` interface grants temporary bridge
control and restores the setup setting on release. It has no dependency on the
cable tester or another module. The target supplies host state and activity
events and registers the module in the existing menu.

The shared bridge reports packet completion before queue acceptance and also
parses physical input while enabled without a configured USB host. It does not
queue that disconnected input for later replay. Output events come only after a
successful physical transmitter submission. The module repaints once per poll
when visible state changes, not for every received byte or refreshed pulse.

Native tests cover lifecycle and setup restoration, host status, independent
pulses, expiry and timer wraparound, stale-state clearing, queue overflow and
physical parsing without a host. Firmware builds use isolated output paths;
no device access or upload is part of this implementation's verification.
