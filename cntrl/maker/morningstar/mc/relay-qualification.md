# Relay qualification status

The generic relay configuration and runtime are implemented, but this target
declares no qualified relay contacts. Relay configurations are rejected and no
relay GPIOs are enabled by this feature.

The [pin map](mc6-pro-pin-map.md#relay-outputs-confirmed) records four stock
firmware labels and logical GPIO pins: A tip 35, A ring 2, B tip 40 and B ring 41.
These are firmware traces, not electrical qualification. Contact polarity,
driver inversion, power-up state, relay-coil circuitry and connection ratings
remain unverified. No voltage, current or amplifier-connection limit can yet be
stated from the repository evidence.

Before enabling a driver, establish each physical contact's identity and
normally open/closed behavior, confirm the logical engagement polarity, measure
resting and pulse behavior including startup/setup changes, and establish the
isolation and allowable electrical loads from reliable hardware evidence.
Qualification must include independent tip/ring operation and restoration
while MIDI output is blocked. Keep attached equipment disconnected until those
properties are established.

Once qualified, the adapter can declare contact names and map the portable
`RelayDriver` API to those outputs. The [generic configuration guide](../../../../doc/relay-outputs.md)
describes software behavior. This document does not authorize enabling the
currently unqualified GPIO mappings.
