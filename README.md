# cntrl

Hardware-independent descriptions of MIDI controllers, using Pydantic data models.
The hardware model contains controls, displays and wired connectors, not firmware
features, installed firmware, or active settings. The independent
[patch-switcher application](firmware/README.md) adds a portable C++ controller
and Python compiler/upload tools. There are no manufacturer patch importers.

Manufacturer-specific catalogues, firmware and analysis live in their maker
packages. See [Morningstar MC](cntrl/maker/morningstar/mc/README.md).

The application supports named performance setups, on-controller selection and
numeric editing, per-setup graphics, and separate original/program/show patch
names. Named pages, windows and per-line styles are described in the
[display-format design](plan/display-format.md). The default patch page retains
the existing navigation-heading/name UI. Programs and patches can independently
carry multiple reusable categories, with inherited membership and prioritized
category styles. The current compiled document format is version 14; source data must be
recompiled for a format change. Firmware images and setup data are deployed
separately. The [patch-switcher plan](plan/patch-switcher.md) and
[multiple-setups plan](plan/multiple-setups.md) describe implemented behavior;
remaining hardware qualification is tracked in the maker documentation.

## Controls and displays

- `Button`: press and release events.
- `AbsoluteControl`: a position within a declared `ValueRange`, such as a
  potentiometer or expression pedal.
- `RelativeControl`: direction or steps from an endless dial. An encoder's push
  switch is a separate `Button`.
- `Display`: numeric, alphanumeric, or graphical. Width and height measure
  character cells for text displays, pixels for graphical displays. Unknown
  dimensions and foreground colour counts are `None`.

## Ports

Each `Port` has a `PhysicalSpecification` and a non-empty list of `PortMode`
objects describing supported electrical uses. Direction is relative to the
controller. `port.configurable` is true when several modes are supported,
whether selection uses software or hardware switches. No mode is selected by
the description.

Connector type and protocol are separate. A TRS connector can carry MIDI, sense
an expression pedal, or sense switch contacts. MIDI modes may specify wiring.
USB ports are single `UsbSocket` enum values, such as `"usb_b"` or `"usb_c"`.
An explicit host declaration uses `UsbPort`, containing only its socket kind
and `host` flag. Ethernet does not enumerate
application protocols. CV declares a voltage range; relay outputs are distinct from switch
inputs.

Power-only connectors are excluded. Combined USB power/data connectors remain
included. Wireless radios are outside this wired-connector model.

Models use `frozen=True` to prevent field reassignment; list members are not
deeply immutable. JSON serialization uses `model_dump_json()` and
`Controller.model_validate_json()`. Import symbols from their defining modules;
there are no package-level re-exports.

## Data-only hardware catalogues

A catalogue file declares models in tables such as `[example]`.
Multiline nested dictionaries are parsed as TOML 1.1 by tomlkit.
Compound table headers are allowed. Dotted assignments and wrapper tables remain
excluded by the repository instructions.

Tables starting with `@` define shared ports and are excluded from the returned
controllers. A port can reference a shared definition, use a shorthand string,
or provide its properties inline:

```toml
['@trs']
kind = 'midi'
physical = {connector = 'trs', size = '3.5'}

[example]
name = 'Example'
buttons = 'ABC'
displays = ['24', '200x200']

[example.ports]
midi-in = 'midi-in'
usb = 'usb_c'
usb-host = {kind = 'usb_c', host = true}
trs-in = '@trs-in'
trs-out = '@trs-out'
```

`@trs-in` and `@trs-out` apply a direction to `@trs`. `midi-in` is shorthand
for `{kind = 'midi', direction = 'in'}` and uses a DIN connector. Shared ports
use `kind` for one electrical use or `kinds` for several, `physical.size` for
the jack diameter in millimetres, and optional `wiring` for MIDI wiring choices.
A list repeats ports, giving them IDs such as `configurable-1` and `configurable-2`.

A one-dimensional display string describes a monochrome alphanumeric display;
two dimensions describe graphical pixels with an unknown palette size. An
explicit dictionary can override these defaults, for example
`{size = '24x2', kind = 'alphanumeric', color = 1}` for a two-row character LCD.
`{kind = 'graphical'}` keeps undocumented dimensions unknown.

```python
from pathlib import Path

from cntrl.catalogue import load_catalogue

catalogue = load_catalogue(Path('controllers.toml'))
controller = catalogue['example']
```

The loader returns a dictionary of validated `Controller` objects. Repeated
hardware has independent mutable lists. There are no catalogue wrapper classes
or component-reference objects.

## Development

Configuration values with units use `reccy.configuration.units`. Bare numbers
keep their existing units. Physical connector diameters accept lengths such as
`'3.5 mm'`; CV ranges accept voltages such as `'-5 V'` and `'5000 mV'`.
Graphical display dimensions accept pixels, including in catalogue shorthand
such as `'160 px x 128 px'`. Alphanumeric dimensions remain character counts.
Counts, MIDI addresses, raw MIDI values and color values retain their existing
formats.

Requires Python 3.13 or later and uv.

```bash
uv sync
uv run pytest
uv run ruff check --select B,E,F,I cntrl test
uv run ruff format --check cntrl test
uv run ty check cntrl test
```
