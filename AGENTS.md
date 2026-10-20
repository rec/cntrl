# Hardware catalogue invariant

Each controller has exactly one simple TOML table header, such as `[mc3]` or
`[mc6_mkii]`. Keep all its data within that section using dotted assignments
and inline values. Do not add wrapper tables or compound table headers.

Represent USB ports at their point of use as `UsbSocket` enum strings, without
USB protocol, role, direction, or physical-specification objects.
