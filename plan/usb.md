# USB host and hub support

This document records the investigation and proposed implementation of item 5
in [the feature plan](feature-set.md#5-usb-host-and-hub-support). The proposal
requires changes to the shared destination model. Implementation and the
device-binding policy remain undecided. Item 5 stays in the feature plan.

## Intended behavior

Connect multiple USB MIDI instruments or controllers through a USB hub and
address each independently. Reordering connections must preserve destination
identity. Disconnecting one device must leave the others usable. Reconnection
must discard stale performance traffic. Missing or ambiguous destinations must
show their status while the controller continues servicing other inputs and
outputs.

## Current implementation

The investigation inspected the following paths.

- `cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp` creates one
  `USBHost` and one `MIDIDevice`. It creates no `USBHub` drivers. Availability,
  packet submission and disconnect handling use that one MIDI driver.
- `firmware/include/cntrl/midi_outputs.h` exposes one `usb-host` output on
  scheduler route 8. The existing physical and USB-device outputs have their
  own routes.
- `firmware/include/cntrl/midi_bridge.h` has nine destinations and one host
  connection state. Host disconnection clears route 8 and cancels its pending
  owned work. Expanding host support requires independent connection states
  and cancellation for each destination.
- The target polls host input for incoming MIDI Clock. It does not bridge
  general host input. Configurable forwarding belongs to feature-plan item 6.
- Setup compilation, output validation, expression assignments, MIDI Clock
  destinations and target capability declarations use the existing output
  names and route model. The implementation must update their shared path.

The installed Teensy USBHost_t36 library provides `USBHub` and multiple
`MIDIDevice` driver instances. `USBDriver` exposes vendor ID, product ID and
serial-number accessors. Its protected device record contains a USB address,
parent hub address and hub port. The driver also exposes the MIDI cable number
for received messages and accepts a cable number when encoding outgoing
packets. These provide the inputs for binding, but the current target does
not use them to identify multiple destinations.

USB addresses are assigned during enumeration. Binding by physical connection
requires reconstructing a hub-port path rather than persisting those addresses.

## Proposed design

The proposed initial capacity is four hosted MIDI devices through up to two
hubs. These are proposed firmware resource limits. Supported physical hubs,
power arrangements and device combinations still require qualification.

Provide configurable hosted-device destinations, each with an independent
scheduler route and connection state. A binding identifies the USB device by
vendor/product IDs and, when available, its serial number. It also selects the
device's MIDI cable number. A USB device and a MIDI cable are separate parts
of the identity, so discovery of another device must not change either part
of an existing destination's binding.

Resolve bindings against the connected devices before marking a destination
available. Clear queued traffic and cancel pending owned work when its binding
disappears or changes. Reconnection requires fresh performance actions.
Service each host driver within the existing bounded polling budget and
preserve fairness between destinations under congestion.

Expose connected, missing and ambiguous destination states. Reject bindings
that exceed target capabilities during compilation. Reuse the existing setup
models, compiler, editable export, history comparison, patch executor and MIDI
scheduler. Keep USB enumeration and hub access in the Morningstar target
adapter.

The setup schema, destination names, treatment of the existing `usb-host`
destination and compiled representation need to be settled before coding.
Choose one intended configuration approach and update all consumers together.
Account for scheduler queues, driver buffers and hub objects in the target's
memory budget before fixing the capacity.

## Unresolved device-binding policy

Vendor/product IDs alone cannot distinguish two identical devices. Serial
numbers can be absent or duplicated. Enumeration order must never silently
select an instrument.

Two alternatives were presented for this case.

1. Leave ambiguous destinations unavailable and show their status. This keeps
   configuration smaller and prevents accidental redirection. Simultaneous
   identical devices require distinguishing identity information.
2. Allow explicit hub-port bindings. This supports identical devices without
   unique serial numbers, but the physical hub connections become part of
   the setup. Moving a device to another socket changes which binding can
   match it. Define nested hub paths and identity checks together.

The user requested this investigation document without choosing either policy.
The proposed four-device/two-hub design has also not been approved for
implementation.

## Implementation sequence

1. Settle the ambiguous-device policy, destination names, capacity and cable
   selection rules. Specify behavior for duplicate bindings and for multiple
   MIDI interfaces on one device.
2. Extend the shared output model and setup representation. Add compilation,
   capability validation, editable export and history comparison together.
3. Add independent host routes, connection states and cancellation to the
   scheduler. Update every output consumer, including patches, expression
   assignments and MIDI Clock.
4. Instantiate the host MIDI and hub drivers in the target adapter. Resolve
   bindings, dispatch outgoing packets to the selected device and cable, and
   poll inputs fairly. Define incoming clock-source identity across devices.
5. Add destination status feedback, configuration documentation and focused
   tests. Build the target firmware and measure its resource usage.
6. Qualify physical hub enumeration, routing, disconnection, reconnection and
   power behavior before claiming hardware acceptance.

## Verification

Automated tests should cover reordered enumeration, missing and duplicate
serials, ambiguous matches, cable selection and the chosen port-binding rules.
Exercise disconnect and reconnect with pending MIDI, SysEx and owned patch
work. Confirm that one blocked destination cannot starve others, and that a
replacement device never receives traffic queued for its predecessor.

Check compilation and export/recompile equality for bindings, target capacity
errors and setup changes. Exercise clock output and expression assignments
through the same destination model. Verify bounded polling and memory usage
with all proposed driver slots occupied.

Physical qualification requires the supported hubs and multiple MIDI devices.
Test connection-order changes, simultaneous traffic, disconnecting one device,
hub removal and reconnection, and identical devices under the selected policy.
Record hub models, device counts, cable numbers and power arrangements. Verify
traffic at the intended receiver, including isolation between destinations.
Firmware flashing must be coordinated with other agents using the shared
pedal.

The investigation accessed source files and the installed USB library. It
performed no device enumeration, USB transfers or firmware flashing. Physical
support and power limits remain unverified.

## Additional work beyond the prompt

None.
