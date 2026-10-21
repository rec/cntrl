# cntrl

Hardware-independent descriptions of MIDI controllers, using Pydantic data models.
The hardware model contains controls, displays and wired connectors, not firmware
features, installed firmware, or active settings. There are no drivers, editors,
or manufacturer patch importers.

A separate [USB-only experimental MC6 Pro firmware target](firmware/README.md)
has build tooling and static inspection results. Its first USB bring-up succeeded;
an [upload-only helper](firmware/upload-helper.md) supports the next transport test.

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

`cntrl/data/morningstar.toml` declares models in tables such as `[mc3]`.
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
A list repeats ports, giving them IDs such as `omniport-1` and `omniport-2`.

A one-dimensional display string describes a monochrome alphanumeric display;
two dimensions describe graphical pixels with an unknown palette size. An
explicit dictionary can override these defaults, for example
`{size = '24x2', kind = 'alphanumeric', color = 1}` for a two-row character LCD.
`{kind = 'graphical'}` keeps undocumented dimensions unknown.

```python
from cntrl.catalogue import load_catalogue

catalogue = load_catalogue('morningstar')
controller = catalogue['mc6_pro']
```

The loader returns a dictionary of validated `Controller` objects. Repeated
hardware has independent mutable lists. There are no catalogue wrapper classes
or component-reference objects.

| Model key | Buttons | Displays | Wired ports |
| --- | ---: | --- | ---: |
| `mc3` | 3 | 1 character LCD, 20 x 2 | 7 |
| `mc6_mkii` | 6 | 1 character LCD, 24 x 2 | 5 |
| `mc8` | 8 | 1 character LCD, 40 x 4 | 7 |
| `mc4_pro` | 4 | 1 colour LCD | 9 |
| `mc6_pro` | 6 | 3 colour LCDs | 12 |
| `mc8_pro` | 8 | 4 colour LCDs | 12 |

Sources are the manufacturer's specifications for
[MC3](https://www.morningstar.io/mc3),
[MC6 MkII](https://www.morningstar.io/mc6-mkii),
[MC8](https://www.morningstar.io/mc8-midi-controller),
[MC4 Pro](https://www.morningstar.io/mc4-pro),
[MC6 Pro](https://www.morningstar.io/mc6-pro), and
[MC8 Pro](https://www.morningstar.io/mc8-pro).
Further detail comes from the manuals for
[MC3](https://help.morningstar.io/en/article/mc3-user-manual-19hvw2w/),
[MC4 Pro](https://help.morningstar.io/en/article/mc4-pro-user-manual-1p5o2cp/),
[MC6 Pro](https://help.morningstar.io/en/article/mc6-pro-user-manual-psevjy/), and
[MC8 Pro](https://help.morningstar.io/en/article/mc8-pro-user-manual-1yqqr1r/).
The MC3's third and fourth TRS
MIDI outputs have DIP-selectable wiring. MC4 Pro has separate rear and side TRS
MIDI inputs and two different configurable-port jack sizes.

MC6 Pro uses the user-supplied 200 x 200 display dimensions. MC4 Pro and MC8 Pro
dimensions and all Pro palette sizes remain unknown, with TODOs in the data.
Physical DIN contact counts also remain unknown pending confirmation.
External expression pedals and switches are not built-in controls. No firmware
restrictions, presets, calibration, or live device state are represented.

## Development

Requires Python 3.13 or later and uv.

```bash
uv sync
uv run pytest
uv run ruff check --select B,E,F,I cntrl test
uv run ruff format --check cntrl test
uv run ty check cntrl test
```
