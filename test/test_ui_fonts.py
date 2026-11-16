"""Portable UI font preparation and declared family availability."""

import struct
from pathlib import Path
from typing import Literal
from zlib import crc32

import pytest
import tomlkit
from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.fonts import assets, status, ui
from cntrl.fonts.bitmap import BitmapRaster
from cntrl.fonts.catalogue import bundled_sources
from cntrl.fonts.outline import Glyph, Outline
from cntrl.patch_switcher import compiler, data_files, models, page_settings
from scripts import native_tests


@pytest.mark.parametrize(
    'name',
    ['AtkinsonNextSemiBold', 'CourierPrime', 'CourierPrimeBold', 'Arimo', 'ArimoBold'],
)
@pytest.mark.parametrize('hinting', ['auto', 'native'])
def test_ui_fonts_preserve_hinted_pixels_bearings_and_advances(
    name: str, hinting: Literal['auto', 'native'], tmp_path: Path
) -> None:
    data = ui.prepare(name, [12, 20], hinting=hinting)
    resource = tmp_path / 'prepared.cfont'
    resource.write_bytes(data)
    bitmap = BitmapRaster(resource)
    face = assets.decode(data, name, crc32(data))
    source = Outline(bundled_sources()[name], hinting=hinting)
    for size in [12, 20]:
        assert set(bitmap.characters[size]) == set(range(32, 127))
        assert not face.sizes[size].runs
        assert (
            face.sizes[size].top
            == source.glyph(size, source.face.get_char_index(ord('H'))).top
        )
        for code in range(32, 127):
            expected = source.glyph(size, source.face.get_char_index(code))
            actual = bitmap.glyph(size, code)
            assert actual.pixels == expected.pixels
            assert (actual.width, actual.height, actual.left, actual.top) == (
                expected.width,
                expected.height,
                expected.left,
                expected.top,
            )
            assert actual.advance == round(expected.advance)


def test_tamzen_utility_asset_preserves_native_pixels_and_advances() -> None:
    directory = ROOT / 'cntrl/fonts/bitmap_samples'
    original = BitmapRaster(directory / 'Tamzen-Tamzen7x13b.cfont')
    utility = BitmapRaster(directory / 'TamzenBold7x13.cfont')

    def ink(glyph: Glyph) -> set[tuple[int, int]]:
        return {
            (x + glyph.left, y - glyph.top)
            for y in range(glyph.height)
            for x in range(glyph.width)
            if glyph.pixels[y * ((glyph.width + 7) // 8) + x // 8] & (128 >> (x % 8))
        }

    for code in range(32, 127):
        source, actual = original.glyph(13, code), utility.glyph(13, code)
        assert ink(actual) == ink(source)
        assert actual.advance == source.advance == 7
    assert utility.glyph(13, ord('H')).height == 7
    assert set(utility.characters[13]) == set(map(ord, status.BASE_CHARACTERS))
    assert len(utility.characters[13]) == 139
    assert (directory / 'TamzenBold7x13.cfont').read_bytes() == status.prepare()


def test_status_base_glyphs_render_at_the_native_baseline(
    tmp_path: Path, file_regression: FileRegressionFixture
) -> None:
    font = BitmapRaster(ROOT / 'cntrl/fonts/bitmap_samples/TamzenBold7x13.cfont')
    image = Image.new('1', (20 * 9, 7 * 16), 0)
    for i, c in enumerate(status.BASE_CHARACTERS):
        glyph = font.glyph(13, ord(c))
        assert glyph.advance == 7
        for y in range(glyph.height):
            for x in range(glyph.width):
                if glyph.pixels[y * ((glyph.width + 7) // 8) + x // 8] & (
                    128 >> (x % 8)
                ):
                    image.putpixel(
                        (i % 20 * 9 + x, i // 20 * 16 + 11 - glyph.top + y), 1
                    )
    path = tmp_path / 'status-base.png'
    image.resize((image.width * 4, image.height * 4), Image.Resampling.NEAREST).save(
        path
    )
    file_regression.check(path.read_bytes(), binary=True, extension='.png')


def test_ui_font_header_compiles_and_renders_without_a_driver_sdk(
    tmp_path: Path,
) -> None:
    data = ui.prepare('AtkinsonNextSemiBold', [12])
    (tmp_path / 'ui_fonts.h').write_text(
        ui.embedded_header({'AtkinsonNextSemiBold': data})
    )
    executable = tmp_path / 'font-provider'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            f'-I{ROOT / "firmware/include"}',
            f'-I{tmp_path}',
            str(ROOT / 'firmware/test/font_asset_compile.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    native_tests.run_native([str(executable)], check=True)


def test_optional_classic_font_can_be_prepared_from_an_explicit_outline() -> None:
    with pytest.raises(ValueError, match='requires an outline source'):
        ui.prepare('classic', [12])
    assert ui.prepare(
        'classic', [12], bundled_sources()['AtkinsonNextSemiBold']
    ).startswith(b'CFN2')


def test_french_runtime_glyphs_render_without_replacing_authored_fonts(
    tmp_path: Path, file_regression: FileRegressionFixture
) -> None:
    characters = (
        ''.join(chr(c) for c in range(32, 127)) + 'ÀÂÇÉÈÊËÎÏÔÙÛÜŒàâçéèêëîïôùûüœ…’'
    )
    english = tmp_path / 'english.cfont'
    english.write_bytes(ui.prepare('Arimo', [8, 10, 12, 14, 16, 20], hinting='native'))
    french = tmp_path / 'french.cfont'
    data = ui.prepare(
        'Arimo',
        [8, 10, 12, 14, 16, 20],
        hinting='native',
        characters=characters,
    )
    # Exercise a language-only letter and punctuation alongside the base.
    supplement = status.prepare('ã’')
    data = assets.HEADER.pack(b'CFN2', 7) + data[8:] + supplement[8:]
    french.write_bytes(data)
    face = assets.decode(data, 'Français', crc32(data))
    assert set(face.sizes[12].characters) == set(characters)
    print(f'French UI font working set: {len(data)} bytes')
    program = tmp_path / 'localized-fonts'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{ROOT / "firmware/include"}',
            str(ROOT / 'firmware/test/test_localized_fonts.cpp'),
            '-o',
            str(program),
        ],
        check=True,
    )
    rendered = tmp_path / 'french.ppm'
    native_tests.run_native(
        [
            str(program),
            str(english),
            str(french),
            str(rendered),
            str(ROOT / 'cntrl/fonts/bitmap_samples/TamzenBold7x13.cfont'),
        ],
        check=True,
    )
    screenshot = tmp_path / 'french.png'
    Image.open(rendered).save(screenshot)
    file_regression.check(screenshot.read_bytes(), binary=True, extension='.png')


def test_page_fonts_use_declared_ui_families_without_entering_patch_picker() -> None:
    data = compiler.read_document(ROOT / 'firmware/test/target.json')
    assert isinstance(data, dict)
    target = models.Target.model_validate(data)
    assert not any(f.name == 'AtkinsonNextSemiBold' for f in target.fonts)
    show = models.Setup.model_validate({})
    words = page_settings.pack(show, target)
    assert words[14] == 8 and words[15] == 19 and words[22] == 16
    restricted = target.model_copy(
        update={
            'ui_fonts': [
                'classic',
                'CourierPrime',
                'AtkinsonNextSemiBold',
                'TamzenBold7x13',
            ]
        }
    )
    show = models.Setup.model_validate({'page_settings': {'title_font': 'Arial'}})
    with pytest.raises(ValueError, match='title_font: unavailable font'):
        page_settings.pack(show, restricted)
    with pytest.raises(ValueError, match='distinct canonical'):
        models.Target.model_validate({**data, 'ui_fonts': ['classic', 'Unknown']})
    with pytest.raises(ValueError, match='classic UI font is required'):
        models.Target.model_validate({**data, 'ui_fonts': ['CourierPrime']})


@pytest.mark.parametrize('family', ['Portable', 'CourierPrime'])
@pytest.mark.parametrize('scale', [1.0, 1.25])
def test_relocated_outline_resources_are_used_at_every_scale(
    family: str, scale: float, tmp_path: Path
) -> None:

    exported = tmp_path / 'export'
    fonts = exported / 'fonts'
    fonts.mkdir(parents=True)
    source = bundled_sources()['AtkinsonNextSemiBold']
    (fonts / 'portable.ttf').write_bytes(source.read_bytes())
    data = compiler.read_document(ROOT / 'firmware/test/target.json')
    assert isinstance(data, dict)
    data['font_sources'] = {family: 'fonts/portable.ttf'}
    (exported / 'target.toml').write_text(tomlkit.dumps(data))
    relocated = tmp_path / 'relocated'
    exported.rename(relocated)
    errors: list[str] = []
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(name='target', paths=[relocated / 'target.toml']),
            False,
            errors,
        )
    )
    assert not errors
    prepared = assets.Preparation(
        models.Setup(),
        target,
        [
            (
                255,
                'Café',
                models.Style(font_name=family, horizontal_scale=scale),
                4,
                1,
                0,
            )
        ],
        ['title'],
    )
    name = family if scale == 1 else f'{family}@{scale}x'
    raster = prepared.outlines[name]
    assert isinstance(raster, Outline)
    expected = Outline(relocated / 'fonts/portable.ttf', scale)
    assert raster.shape(20, 'Café') == expected.shape(20, 'Café')
    prepared.use(name, 20, 255, 'Café')
    _, keys, files = prepared.finish()
    assert name in keys and files[keys[name]].startswith(b'CFN2')


def test_all_canonical_ui_ids_round_trip_for_each_page_role() -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    show = models.Setup.model_validate(
        {
            'page_settings': {
                'tempo_font': 'Arial',
                'status_font': 'CourierPrimeBold',
                'title_font': 'StillTime',
            }
        }
    )
    words = page_settings.pack(show, target)
    assert words[14] == 1 and words[15] == 9 and words[22] == 5
    restored = page_settings.unpack(struct.pack('<23H', *words), 0)
    assert (
        restored['tempo_font'] == 'Arial'
        and restored['status_font'] == 'CourierPrimeBold'
        and restored['title_font'] == 'StillTime'
    )


ROOT = Path(__file__).parents[1]
