"""Portable UI font preparation and declared family availability."""

import struct
import subprocess
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit

from cntrl.fonts import assets, ui
from cntrl.fonts.bitmap import BitmapRaster
from cntrl.fonts.catalogue import bundled_sources
from cntrl.fonts.outline import Outline
from cntrl.patch_switcher import compiler, data_files, models, page_settings


@pytest.mark.parametrize(
    'name', ['AtkinsonNextSemiBold', 'CourierPrime', 'CourierPrimeBold']
)
def test_ui_fonts_preserve_hinted_pixels_bearings_and_advances(
    name: str, tmp_path: Path
) -> None:
    data = ui.prepare(name, [12, 20])
    resource = tmp_path / 'prepared.cfont'
    resource.write_bytes(data)
    bitmap = BitmapRaster(resource)
    face = assets.decode(data, name, crc32(data))
    source = Outline(bundled_sources()[name], autohint=True)
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


def test_ui_font_header_compiles_and_renders_without_a_driver_sdk(
    tmp_path: Path,
) -> None:
    data = ui.prepare('AtkinsonNextSemiBold', [12])
    (tmp_path / 'ui_fonts.h').write_text(
        ui.embedded_header({'AtkinsonNextSemiBold': data})
    )
    executable = tmp_path / 'font-provider'
    subprocess.run(
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
    subprocess.run([str(executable)], check=True)


def test_optional_classic_font_can_be_prepared_from_an_explicit_outline() -> None:
    with pytest.raises(ValueError, match='requires an outline source'):
        ui.prepare('classic', [12])
    assert ui.prepare(
        'classic', [12], bundled_sources()['AtkinsonNextSemiBold']
    ).startswith(b'CFN2')


def test_page_fonts_use_declared_ui_families_without_entering_patch_picker() -> None:
    data = compiler.read_document(ROOT / 'firmware/test/target.json')
    assert isinstance(data, dict)
    target = models.Target.model_validate(data)
    assert not any(f.name == 'AtkinsonNextSemiBold' for f in target.fonts)
    show = models.Setup.model_validate({})
    words = page_settings.pack(show, target)
    assert words[14] == 8 and words[15] == 16 and words[22] == 16
    restricted = target.model_copy(
        update={'ui_fonts': ['classic', 'CourierPrime', 'AtkinsonNextSemiBold']}
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
