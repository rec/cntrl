"""Show graphics resolution and saved placement, independent of a device."""

import struct
import warnings
from pathlib import Path

import pytest

from cntrl.patch_switcher import compiler, graphics, models


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(FIXTURES / 'target.json')
    )


def test_defaults_and_explicit_null_settings_agree(target: models.Target) -> None:
    default = graphics.compile_screens(
        models.Show(instruments={}, patches={}), ['Flute', 'wind'], target
    )
    assert default == graphics.compile_screens(
        models.Show(
            instruments={},
            patches={},
            font_name=None,
            font_size=None,
            pad=None,
            screens=models.Screens(
                next=models.ScreenGraphics(
                    foreground_color=None,
                    background_color=None,
                    half=None,
                )
            ),
        ),
        ['Flute', 'wind'],
        target,
    )
    records, offsets = default
    fields = graphics.SCREEN.unpack(records[0])
    assert fields[:3] == (0, 40, 4)
    assert fields[4:8] == (0xFFFF, 0, 40, 0)
    assert fields[8:] == (0x7BEF, 0)
    assert offsets == [[4 + (152 - 150) // 2, 4 + (152 - 120) // 2]] * 3


@pytest.mark.parametrize(
    'name, rgb',
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
            models.ScreenGraphics(foreground_color='red', background_color='red'), 0.6
        )
    with pytest.warns(UserWarning):
        graphics.colors(
            models.ScreenGraphics(foreground_color='black', background_color='white'), 1
        )


def test_one_color_chooses_complement_or_furthest_with_warning() -> None:
    assert graphics.colors(models.ScreenGraphics(foreground_color='red'), 0.6) == (
        0xF800,
        0x07FF,
        0x7BEF,
    )
    assert graphics.colors(models.ScreenGraphics(background_color='red'), 0.6) == (
        0x07FF,
        0xF800,
        0x7BEF,
    )
    with pytest.warns(UserWarning, match='contrast 0.600'):
        assert graphics.colors(models.ScreenGraphics(foreground_color='gray'), 0.6) == (
            graphics.rgb565((128, 128, 128)),
            0,
            graphics.rgb565((64, 64, 64)),
        )
    chosen = graphics.color('0x909090')
    fg, bg, _ = graphics.colors(
        models.ScreenGraphics(foreground_color='0x909090'), 0.41
    )
    assert fg == graphics.rgb565(chosen) and bg != graphics.rgb565((111, 111, 111))


@pytest.mark.parametrize('half', [None, ''])
def test_half_defaults_to_rgb_midpoint(half: str | None) -> None:
    assert graphics.colors(
        models.ScreenGraphics(
            foreground_color='red', background_color='blue', half=half
        ),
        0.6,
    )[2] == graphics.rgb565((127, 0, 127))


@pytest.mark.parametrize('half', ['yellow', '0x112233'])
def test_half_accepts_named_and_hex_colors_without_contrast_warning(half: str) -> None:
    with warnings.catch_warnings(action='error'):
        records, _ = graphics.compile_screens(
            models.Show(
                instruments={},
                patches={},
                screens=models.Screens(next=models.ScreenGraphics(half=half)),
            ),
            [],
            models.Target.model_validate(
                compiler.read_document(FIXTURES / 'target.json')
            ),
        )
    assert graphics.SCREEN.unpack(records[2])[8] == graphics.rgb565(
        graphics.color(half)
    )


def test_screens_share_font_size_and_padding_with_independent_colors(
    target: models.Target,
) -> None:
    show = compiler.read_document(FIXTURES / 'show.json')
    assert isinstance(show, dict)
    show['font_size'] = 16
    show['pad'] = 10
    show['screens'] = {
        'undo': {},
        'sent': {'foreground_color': 'red'},
        'next': {'background_color': 'blue'},
    }
    data = compiler.compile_show(
        show, compiler.read_document(FIXTURES / 'definitions.json'), [], target
    )
    assert [data[29 + 16 * i] for i in range(3)] == [16, 16, 16]
    assert [data[30 + 16 * i] for i in range(3)] == [10, 10, 10]
    category = 76 + 2 * 68
    patch = category + 2 * 74
    assert struct.unpack_from('<3H', data, category + 68)[0] == 4 + (152 - 4 * 12) // 2
    assert struct.unpack_from('<3H', data, patch + 76)[2] == 4 + (152 - 5 * 12) // 2


def test_all_screen_errors_are_reported_together(target: models.Target) -> None:
    show = compiler.read_document(FIXTURES / 'show.json')
    assert isinstance(show, dict)
    show['screens'] = {
        'undo': {'foreground_color': 'missing'},
        'sent': {'half': 'missing'},
        'next': {'background_color': 'missing'},
    }
    with pytest.raises(compiler.CompilationError) as error:
        compiler.compile_show(
            show, compiler.read_document(FIXTURES / 'definitions.json'), [], target
        )
    assert all(f'screens.{s}' in str(error.value) for s in ('undo', 'sent', 'next'))


def test_auto_size_fits_shared_padding(target: models.Target) -> None:
    records, offsets = graphics.compile_screens(
        models.Show(
            instruments={},
            patches={},
            pad=20,
        ),
        ['Flute'],
        target,
    )
    assert [graphics.SCREEN.unpack(r)[1] for r in records] == [32, 32, 32]
    assert offsets == [[20], [20], [20]]


def test_padding_must_leave_drawable_area(target: models.Target) -> None:
    with pytest.raises(ValueError, match='padding leaves no drawable display area'):
        graphics.compile_screens(
            models.Show(instruments={}, patches={}, pad=80), ['Flute'], target
        )


def test_metrics_account_for_negative_bearings() -> None:
    data = bytearray([6, 0, 6] * 95)
    data[(ord('j') - 32) * 3 : (ord('j') - 32) * 3 + 3] = bytes([5, 255, 5])
    assert graphics.bounds(
        'jA', models.FontMetrics(line_height=8, glyphs=data.hex())
    ) == (-1, 12)


FIXTURES = Path(__file__).resolve().parents[1] / 'firmware/test'
