# Hardware catalogue invariant

Each controller has exactly one simple TOML table header, such as `[mc3]` or
`[mc6_mkii]`. Keep all its data within that section using nested dictionaries
with readable line lengths. Do not use dotted assignments, wrapper tables, or
compound table headers. Multiline dictionaries use TOML 1.1, parsed by tomlkit.

Represent USB ports at their point of use as `UsbSocket` enum strings, without
USB protocol, role, direction, or physical-specification objects.
