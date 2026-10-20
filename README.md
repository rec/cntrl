# cntrl

Hardware-independent descriptions of MIDI controllers, using Pydantic data models.
Editors can inspect a controller's capabilities without knowing its hardware.
Hardware-specific implementations will implement the corresponding operations.
This initial project describes capabilities only; it does not contain drivers,
an editor, or support for importing manufacturers' patches.

## Controls and displays

- `Button`: press and release events.
- `AbsoluteControl`: a position within a declared `ValueRange`, such as a
  potentiometer or expression pedal.
- `RelativeControl`: direction or steps from an endless dial, without an
  absolute position. An encoder's push switch is a separate `Button`.
- `Display`: numeric, alphanumeric, or graphical, with one to three selectable
  foreground colours. Width and height measure character cells for numeric and
  alphanumeric displays, and pixels for graphical displays.

## Ports

Each `Port` has a `PhysicalSpecification`, a non-empty list of supported
`PortConfiguration` objects, and an active configuration ID. Direction is
relative to the controller. A port is fixed when it has one configuration;
`port.configurable` is true when it has several. These are called
**configurable ports**, independently of manufacturer.

Physical connector type and protocol are separate. A TRS connector can carry
MIDI, sense an expression pedal, or sense switch contacts according to its
configuration. MIDI configurations can specify TRS Type A or Type B wiring.
USB configurations describe host/device role and MIDI/HID functions. Ethernet
configurations can name higher-level protocols. CV configurations declare a
voltage range.

Power-only connectors are deliberately excluded. A combined USB power/data
connector is represented as a USB data connection.

Absent capabilities are represented by empty lists, rather than placeholder
controls or ports. Models use `frozen=True` to prevent field reassignment;
their list members are not deeply immutable. Use `model_dump_json()` and
`Controller.model_validate_json()` for JSON serialization and validation.

Import symbols from their defining modules, `cntrl.models` and `cntrl.enums`.
There are no package-level re-exports.

## Development

Requires Python 3.13 or later and uv.

```bash
uv sync
uv run pytest
uv run ruff check --select B,E,F,I cntrl test
uv run ruff format --check cntrl test
uv run ty check cntrl test
```
