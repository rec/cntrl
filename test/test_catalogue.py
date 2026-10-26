from pathlib import Path

import pytest

from cntrl import catalogue, enums, models


def test_display_shorthand_and_connector_size_accept_units(tmp_path: Path) -> None:
    path = tmp_path / 'units.toml'
    path.write_text("""
[example]
name = 'Example'
buttons = ''
displays = ['160 px x 128 px']
ports = {jack = {kind = 'midi-in', physical = {connector = 'trs', size = '0.35 cm'}}}
""")
    controller = catalogue.load_catalogue(path)['example']
    assert (controller.displays[0].width, controller.displays[0].height) == (160, 128)
    port = controller.ports[0]
    assert isinstance(port, models.Port)
    assert port.physical.diameter_mm == 3.5


def test_shorthand_and_explicit_definitions_resolve_equivalently(
    tmp_path: Path,
) -> None:
    path = tmp_path / 'example.toml'
    path.write_text("""
['@jack']
kind = 'midi'
physical = {connector = 'trs', size = '3.5'}

[example]
name = 'Example'
buttons = 'A'
displays = ['24', '200x100', {size = '24x2', kind = 'alphanumeric', color = 3}]

[example.ports]
input = '@jack-in'
explicit = {
    kind = 'midi', direction = 'in',
    physical = {connector = 'trs', size = '3.5'},
}
output = '@jack-out'
midi = 'midi-in'
usb = 'usb_c'
usb-host = {kind = 'usb_c', host = true}
""")
    controller = catalogue.load_catalogue(path)['example']
    first, explicit, output, midi = controller.ports[:4]
    assert isinstance(first, models.Port)
    assert isinstance(explicit, models.Port)
    assert isinstance(output, models.Port)
    assert isinstance(midi, models.Port)
    assert first.physical == explicit.physical
    assert first.modes == explicit.modes
    assert first.modes[0].direction == enums.Direction.INPUT
    assert output.modes[0].direction == enums.Direction.OUTPUT
    assert midi.physical.connector == enums.ConnectorKind.DIN
    assert first.physical.diameter_mm == 3.5
    assert controller.displays[0].kind == enums.DisplayKind.ALPHANUMERIC
    assert controller.displays[0].colors == 1
    assert controller.displays[0].height is None
    assert controller.displays[1].kind == enums.DisplayKind.GRAPHICAL
    assert controller.displays[1].height == 100
    assert controller.displays[1].colors is None
    assert controller.displays[2].kind == enums.DisplayKind.ALPHANUMERIC
    assert controller.displays[2].height == 2
    assert controller.displays[2].colors == 3


@pytest.mark.parametrize('reference', ['@missing', '@missing-in'])
def test_unknown_shared_definition_is_rejected(tmp_path: Path, reference: str) -> None:
    path = tmp_path / 'example.toml'
    path.write_text(f"""
[example]
name = 'Example'
buttons = ''
displays = []
ports = {{jack = '{reference}'}}
""")
    with pytest.raises(KeyError, match='missing'):
        catalogue.load_catalogue(path)
