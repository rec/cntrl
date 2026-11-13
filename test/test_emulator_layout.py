"""The emulator enclosure follows catalogue IDs and dimensionless geometry."""

from pathlib import Path

import pytest
from pydantic import ValidationError

from cntrl.emulator.layout import Canvas, EmulatorLayout


@pytest.fixture
def layout(tmp_path: Path) -> EmulatorLayout:
    (tmp_path / 'catalogue.toml').write_text(
        "[unit]\nname = 'Example controller'\nbuttons = 'A'\n"
        "displays = ['20x10']\n[unit.ports]\nusb = 'usb_c'\n"
    )
    path = tmp_path / 'layout.toml'
    path.write_text(
        "catalogue = 'catalogue.toml'\ncontroller = 'unit'\nwidth = 100\nheight = 50\n"
        '[controls]\na = {x=10, y=10, width=20, height=10}\n'
        '[displays]\nlcd-1 = {x=40, y=5, width=40, height=20}\n'
    )
    return EmulatorLayout.load(path)


def test_layout_references_the_catalogue_relative_to_its_file(
    layout: EmulatorLayout,
) -> None:
    controller = layout.hardware()
    assert controller.name == 'Example controller'
    assert controller.controls[0].id == 'a'
    assert controller.displays[0].width == 20


def test_hit_testing_uses_uniform_scale_and_letterboxing(
    layout: EmulatorLayout,
) -> None:
    canvas = Canvas(layout, 200, 200)
    assert canvas.control(25, 75) == 'a'
    assert canvas.control(20, 20) is None
    assert canvas.control(60, 75) is None
    assert Canvas(layout, 100, 50).control(12, 12) == 'a'


def test_every_component_must_be_placed(layout: EmulatorLayout) -> None:
    with pytest.raises(ValueError, match='every controller control and display'):
        layout.model_copy(update={'displays': {}}).hardware()


def test_placement_cannot_extend_beyond_the_canvas(layout: EmulatorLayout) -> None:
    data = layout.model_dump()
    data['controls']['a']['width'] = 200
    with pytest.raises(ValidationError, match='beyond the emulator canvas'):
        EmulatorLayout.model_validate(data)
