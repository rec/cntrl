# Hardware catalogue invariant

Use nested dictionaries with readable line lengths for hardware declarations.
Do not use dotted assignments or wrapper tables. Compound table headers are
allowed. Multiline dictionaries use TOML 1.1, parsed by tomlkit.

Represent USB ports at their point of use as `UsbSocket` enum strings, without
USB protocol, role, direction, or physical-specification objects.
