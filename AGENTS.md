# Data file formats

Do not create `.json` files unless explicitly requested by the user. Prefer
TOML for configuration and structured data.

# Avoid special purpose scripts

`cntrl` already has commands that accomplish nearly every task you want. Please use
these and avoid creating special-purpose scripts. You can add new functionality to
`cntrl`, but avoid hard-coding filenames or directories there.

AVOID APPROVALS WHENEVER YOU CAN.

# Open the emulator without prompting for any change which affects the GUI in any way

# Hardware catalogue invariant

Code should try to be generic and write to the generic API of a MIDI controller,
with specific implementations for specific hardware platforms.

Use nested dictionaries with readable line lengths for hardware declarations.
Do not use dotted assignments or wrapper tables. Compound table headers are
allowed. Multiline dictionaries use TOML 1.1, parsed by tomlkit.

Represent ordinary USB ports at their point of use as `UsbSocket` enum strings.
Use `{kind = 'usb_c', host = true}` for an explicit host socket. Do not add
USB protocol, direction, or physical-specification objects.

Use `@`-prefixed tables for shared port definitions and references. Repeated
hardware is a list of references. Controller tables contain button labels,
display shorthand, and port declarations. Keep the catalogue format compact.

# Respect intellectual property

Great care should be taken to never store, preserve or distribute any intellectual
property that is not free and open source.

Firmware needs to be handled particularly carefully. No copies, original or derivative,
are to be preserved or distributed. No analysis of that firmwave may be performed except
as needed to establish the functioning of the hardware system.

All device-specific code, tests, data, analysis and documentation must be kept in a
manufacturer-specific directory, and a device-specific subdirectory. For example, work
specific to the Morningstar MC Pro series of footpedals must live under
`cntrl/maker/morningstar/mc/`.  As much code as possible should be generic and
applicable to any MIDi controller.

# Emulator fidelity

The emulator must execute the shared firmware UI and MIDI runtime. Do not add
emulator-only page implementations or silently remove pages and capabilities
that the selected controller supports.

Keep logical control IDs separate from the physical labels shown in captions
and instructions. Emulated controls must display the same labels as the pedal.

Keep setup-file testing distinct from live pedal mirroring. A mirrored session
must use the pedal's compiled setups, selected setup, stored assets, available
language packs and device-wide preferences without recompilation or replacement
with local defaults. Reject unsupported targets or storage formats explicitly.

Represent hardware services that cannot run on the host with inspectable
simulation events. Keep their shared pages and controls available. HID keyboard
simulation must not type into the host; stock-firmware switching must visibly
leave the simulated CNTRL runtime rather than execute a host application.

Add behavior tests for page availability, physical control labels, mirrored
session state and simulated hardware services when changing these paths.
