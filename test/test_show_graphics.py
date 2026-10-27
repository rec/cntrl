"""Color resolution and font metrics shared by the display compiler."""

from pathlib import Path

import pytest

from cntrl.patch_switcher import compiler, graphics, models


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(FIXTURES / 'target.json')
    )


@pytest.mark.parametrize(
    'name,rgb',
    [('RED', (255, 0, 0)), ('0x1234FF', (18, 52, 255)), ('grey', (128, 128, 128))],
)
def test_colors_accept_names_and_hex(name: str, rgb: tuple[int, int, int]) -> None:
    assert graphics.color(name) == rgb


@pytest.mark.parametrize('name', ['0x-00001', '0x12345', '#FF0000', 'unknown'])
def test_invalid_colors_are_errors(name: str) -> None:
    with pytest.raises(ValueError):
        graphics.color(name)


def test_contrast_is_normalized_and_warns_at_threshold() -> None:
    assert graphics.distance((0, 0, 0), (255, 255, 255)) == 1
    with pytest.warns(UserWarning, match='distance 0.000'):
        graphics.colors(
            models.Style(foreground_color='red', background_color='red'), 0.6
        )
    with pytest.warns(UserWarning):
        graphics.colors(
            models.Style(foreground_color='black', background_color='white'), 1
        )


def test_one_color_chooses_complement_or_furthest_with_warning() -> None:
    assert graphics.colors(models.Style(foreground_color='red'), 0.6) == (
        0xF800,
        0x07FF,
    )
    assert graphics.colors(models.Style(background_color='red'), 0.6) == (
        0x07FF,
        0xF800,
    )
    with pytest.warns(UserWarning, match='contrast 0.600'):
        assert graphics.colors(models.Style(foreground_color='gray'), 0.6) == (
            graphics.rgb565((128, 128, 128)),
            0,
        )
    chosen = graphics.color('0x909090')
    fg, bg = graphics.colors(models.Style(foreground_color='0x909090'), 0.41)
    assert fg == graphics.rgb565(chosen) and bg != graphics.rgb565((111, 111, 111))


def test_auto_size_fits_shared_padding(target: models.Target) -> None:
    face, size = graphics.font_choice(
        models.Show(instruments={}, patches={}), ['Flute'], target, 20
    )
    assert size == 32
    assert graphics.bounds('Flute', face.sizes[size]) == (0, 120)


def test_padding_must_leave_drawable_area(target: models.Target) -> None:
    with pytest.raises(ValueError, match='padding leaves no drawable display area'):
        graphics.font_choice(
            models.Show(instruments={}, patches={}), ['Flute'], target, 80
        )


def test_metrics_account_for_negative_bearings() -> None:
    data = bytearray([6, 0, 6] * 95)
    data[(ord('j') - 32) * 3 : (ord('j') - 32) * 3 + 3] = bytes([5, 255, 5])
    assert graphics.bounds(
        'jA', models.FontMetrics(line_height=8, glyphs=data.hex())
    ) == (-1, 12)


FIXTURES = Path(__file__).resolve().parents[1] / 'firmware/test'
