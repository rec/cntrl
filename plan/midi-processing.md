# Remaining MIDI routing qualification

Routing, filtering, remapping, conversion, delay, reverse, scoped generators and
stream gates are implemented. Configuration and runtime limits are documented
in the [usage guide](../doc/midi-processing.md). The remaining acceptance work
below is separate from those completed implementation steps.

## Reconnect stress coverage

The native suite already exercises 300 held-note disconnect/reconnect cycles,
shared note and sustain ownership, blocked fan-out and branch overload. Extend
the combined stress cases to include sustain, partial SysEx, delayed events and
queued cleanup while alternating port order and device identity.

Assert that healthy branches keep progressing, stale connection generations
never write, pending cleanup stays bounded and resources return to baseline
after recovery. Include repeated cable flaps during blocked cleanup and verify
that a replacement device never receives its predecessor's pending traffic.

## Physical qualification

Measure input-scan gaps and MIDI Clock jitter with routing, delay, reverse and
file generators active under output backpressure. Check actual reconnect
behavior during held notes, sustain, SysEx and pending cleanup using an
independent MIDI receiver. Verify that healthy outputs continue and returning
devices receive cleanup before new traffic.

Record endpoint identity, connection generation, receiver captures, timing and
resource measurements. Passive DIN and TRS do not supply reliable disconnect
notifications; exercise observable resets and explicit panic separately.

Multiple hosted USB devices and hubs still depend on the unresolved binding
policy and driver work in [usb.md](usb.md). Automated connection simulations
do not establish physical hub or USB recovery behavior.

## Additional work beyond the prompt

None.
