from importlib.resources import files

import pytest

from cntrl import enums, models
from cntrl.catalogue import load_catalogue


@pytest.fixture
def catalogue() -> dict[str, models.Controller]:
    return load_catalogue(files('cntrl.maker.morningstar.mc').joinpath('mc.toml'))


@pytest.mark.parametrize(
    'name,buttons,displays,ports,configurable',
    [
        ('mc3', 3, 1, 7, 1),
        ('mc6_mkii', 6, 1, 5, 2),
        ('mc8', 8, 1, 7, 4),
        ('mc4_pro', 4, 1, 9, 2),
        ('mc6_pro', 6, 3, 12, 4),
        ('mc8_pro', 8, 4, 12, 4),
    ],
)
def test_documented_hardware_inventory(
    catalogue: dict[str, models.Controller],
    name: str,
    buttons: int,
    displays: int,
    ports: int,
    configurable: int,
) -> None:
    controller = catalogue[name]
    restored = models.Controller.model_validate_json(controller.model_dump_json())
    assert restored == controller
    assert [c.name for c in restored.controls] == list('ABCDEFGH'[:buttons])
    assert all(isinstance(c, models.Button) for c in restored.controls)
    assert len(restored.displays) == displays
    assert len(restored.ports) == ports
    wired = [p for p in restored.ports if isinstance(p, models.Port)]
    assert len({p.id for p in wired}) == len(wired)
    inputs = [
        p
        for p in wired
        if any(m.protocol.kind == enums.ProtocolKind.EXPRESSION for m in p.modes)
    ]
    assert len(inputs) == configurable
    for port in inputs:
        assert port.configurable
        assert {m.protocol.kind for m in port.modes} == {
            enums.ProtocolKind.MIDI,
            enums.ProtocolKind.EXPRESSION,
            enums.ProtocolKind.SWITCH,
        }
        assert all(
            m.direction == enums.Direction.OUTPUT
            if m.protocol.kind == enums.ProtocolKind.MIDI
            else m.direction == enums.Direction.INPUT
            for m in port.modes
        )


@pytest.mark.parametrize(
    'name,width,height', [('mc3', 20, 2), ('mc6_mkii', 24, 2), ('mc8', 40, 4)]
)
def test_character_display_dimensions(
    catalogue: dict[str, models.Controller], name: str, width: int, height: int
) -> None:
    display = catalogue[name].displays[0]
    assert display.kind == enums.DisplayKind.ALPHANUMERIC
    assert display.width == width
    assert display.height == height


@pytest.mark.parametrize('name', ['mc4_pro', 'mc8_pro'])
def test_pro_displays_keep_undocumented_details_unknown(
    catalogue: dict[str, models.Controller], name: str
) -> None:
    for display in catalogue[name].displays:
        assert display.kind == enums.DisplayKind.GRAPHICAL
        assert display.width is None
        assert display.height is None
        assert display.colors is None


def test_mc3_distinguishes_fixed_and_dip_selectable_midi_wiring(
    catalogue: dict[str, models.Controller],
) -> None:
    ports = {p.id: p for p in catalogue['mc3'].ports if isinstance(p, models.Port)}
    for index in [1, 2, 3, 4]:
        port = ports[f'trs-midi-out-{index}']
        assert port.configurable == (index > 2)
        assert all(m.direction == enums.Direction.OUTPUT for m in port.modes)
        assert {
            m.protocol.trs_wiring
            for m in port.modes
            if isinstance(m.protocol, models.MidiProtocol)
        } == (
            {enums.MidiWiring.TYPE_A}
            if index <= 2
            else {
                enums.MidiWiring.TYPE_A,
                enums.MidiWiring.TIP_ACTIVE,
                enums.MidiWiring.RING_ACTIVE,
            }
        )


def test_mc4_pro_has_two_midi_inputs_and_two_configurable_jack_sizes(
    catalogue: dict[str, models.Controller],
) -> None:
    ports = {p.id: p for p in catalogue['mc4_pro'].ports if isinstance(p, models.Port)}
    for name in ['rear-trs-midi-in', 'side-trs-midi-in']:
        assert ports[name].modes[0].direction == enums.Direction.INPUT
        assert isinstance(ports[name].modes[0].protocol, models.MidiProtocol)
        assert ports[name].physical.diameter_mm == 3.5
    assert ports['configurable-1'].physical.diameter_mm == 6.35
    assert ports['configurable-2'].physical.diameter_mm == 3.5


@pytest.mark.parametrize(
    'name,socket,count',
    [
        ('mc3', enums.UsbSocket.USB_B, 1),
        ('mc6_mkii', enums.UsbSocket.USB_B, 1),
        ('mc8', enums.UsbSocket.USB_B, 1),
        ('mc4_pro', enums.UsbSocket.USB_C, 2),
        ('mc6_pro', enums.UsbSocket.USB_C, 2),
        ('mc8_pro', enums.UsbSocket.USB_C, 2),
    ],
)
def test_usb_ports_preserve_socket_types_and_explicit_hosts(
    catalogue: dict[str, models.Controller],
    name: str,
    socket: enums.UsbSocket,
    count: int,
) -> None:
    usb = [
        p
        for p in catalogue[name].ports
        if isinstance(p, enums.UsbSocket | models.UsbPort)
    ]
    assert len(usb) == count
    assert all((p.kind if isinstance(p, models.UsbPort) else p) == socket for p in usb)
    assert sum(isinstance(p, models.UsbPort) and p.host for p in usb) == count - 1


def test_expanded_hardware_has_independent_mutable_members(
    catalogue: dict[str, models.Controller],
) -> None:
    first = catalogue['mc6_pro']
    first.controls.clear()
    ports = [p for p in first.ports if isinstance(p, models.Port)]
    ports[2].modes.clear()
    assert len(ports[3].modes) == 3
    other = [p for p in catalogue['mc8_pro'].ports if isinstance(p, models.Port)]
    assert len(other[2].modes) == 3
    fresh = load_catalogue(files('cntrl.maker.morningstar.mc').joinpath('mc.toml'))[
        'mc6_pro'
    ]
    assert len(fresh.controls) == 6
    assert all(p.modes for p in fresh.ports if isinstance(p, models.Port))


def test_shared_definitions_are_excluded_from_controllers(
    catalogue: dict[str, models.Controller],
) -> None:
    assert len(catalogue) == 6
    assert all(not k.startswith('@') for k in catalogue)


def test_mc6_pro_preserves_user_supplied_display_sizes(
    catalogue: dict[str, models.Controller],
) -> None:
    for display in catalogue['mc6_pro'].displays:
        assert display.kind == enums.DisplayKind.GRAPHICAL
        assert display.width == 200
        assert display.height == 200
        assert display.colors is None


def test_catalogues_use_nested_dicts_and_readable_lines() -> None:
    lines = (
        files('cntrl.maker.morningstar.mc').joinpath('mc.toml').read_text().splitlines()
    )
    assert all(
        '.' not in s.partition('=')[0]
        for s in lines
        if '=' in s and not s.lstrip().startswith('#')
    )
    assert all(len(s) <= 88 for s in lines)
