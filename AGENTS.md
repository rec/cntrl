# Hardware catalogue invariant

Use nested dictionaries with readable line lengths for hardware declarations.
Do not use dotted assignments or wrapper tables. Compound table headers are
allowed. Multiline dictionaries use TOML 1.1, parsed by tomlkit.

Represent ordinary USB ports at their point of use as `UsbSocket` enum strings.
Use `{kind = 'usb_c', host = true}` for an explicit host socket. Do not add
USB protocol, direction, or physical-specification objects.

Use `@`-prefixed tables for shared port definitions and references. Repeated
hardware is a list of references. Controller tables contain button labels,
display shorthand, and port declarations. Keep the catalogue format compact.
