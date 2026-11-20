"""Shared flash fonts keep performance glyphs out of setup RAM."""

import struct
from pathlib import Path

import pytest
from PIL import Image

from cntrl.fonts import assets, ui
from cntrl.fonts.bitmap import BitmapRaster
from cntrl.fonts.catalogue import FONT_IDS, UI_FONTS, bundled_sources
from cntrl.fonts.characters import printable_characters
from cntrl.fonts.outline import Outline
from cntrl.patch_chooser import compiler, display, graphics, models
from scripts import native_tests


@pytest.fixture(scope='module')
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(ROOT / 'patch-chooser-target.json')
    )


@pytest.mark.parametrize('name', list(bundled_sources()))
def test_exported_fonts_preserve_automatically_hinted_supported_characters(
    name: str,
) -> None:
    path = ROOT / 'font_sources' / f'{name}.cfont'
    data = path.read_bytes()
    face = assets.decode(data, name, 0)
    bitmap = BitmapRaster(path)
    outline = Outline(
        bundled_sources()[name],
        horizontal_scale=1.5 if name == 'StillTime' else 1.0,
        hinting='auto',
    )
    expected = {
        c for c in printable_characters() if outline.face.get_char_index(ord(c))
    }
    assert all(set(m.characters) == expected for m in face.sizes.values())
    for size in face.sizes:
        for char in expected:
            source = outline.glyph(size, outline.face.get_char_index(ord(char)))
            actual = bitmap.glyph(size, ord(char))
            assert (
                actual.pixels,
                actual.width,
                actual.height,
                actual.left,
                actual.top,
            ) == (source.pixels, source.width, source.height, source.left, source.top)
            assert actual.advance == round(source.advance)


@pytest.mark.parametrize('name', [n for n in FONT_IDS if n not in UI_FONTS])
def test_performance_fonts_use_the_largest_comfortable_single_size(
    target: models.Target, name: str
) -> None:
    face = next(f for f in target.fonts if f.name == name)
    assert len(face.sizes) == 1
    size, metrics = next(iter(face.sizes.items()))
    assert metrics.line_height <= (target.display_height // 3) * 2 // 3
    if name in bundled_sources():
        outline = Outline(bundled_sources()[name], hinting='auto')
        characters = ui.supported_characters(outline)
        glyphs = [
            outline.glyph(size + 1, outline.face.get_char_index(ord(c)))
            for c in characters
        ]
        height = max(g.top for g in glyphs) + max(
            0, max(g.height - g.top for g in glyphs)
        )
        assert height > (target.display_height // 3) * 2 // 3


@pytest.mark.parametrize('name', [n for n in FONT_IDS if n not in UI_FONTS])
def test_long_performance_labels_keep_the_fixed_font_and_use_no_ram_asset(
    target: models.Target,
    name: str,
) -> None:
    show = models.Setup(style=models.Style(font_name=name), fill_in_styles=False)
    text = 'Wide performance label with a long name'
    files: dict[int, bytes] = {}
    windows, _, block = display.compile_display(
        show,
        [
            display.DisplayPatch(
                values={'patch_cycle.name': 'Main', 'display_name': text}
            )
        ],
        target,
        files,
    )
    assert not files
    assert display.estimate_font_ram(windows, block, files) == 0
    _, styles, _, texts, pages, _ = display.HEADER.unpack_from(block)
    position = display.HEADER.size + styles * graphics.SCREEN.size + pages
    rendered: list[str] = []
    for _ in range(texts):
        length = struct.unpack_from('<H', block, position)[0]
        rendered.append(block[position + 2 : position + 1 + length].decode())
        position += length + 2
    assert any(t.endswith(('…', '...')) for t in rendered)
    assert text not in rendered
    face = next(f for f in target.fonts if f.name == name)
    assert all(
        graphics.bounds(t, next(iter(face.sizes.values())))[1]
        <= target.display_width - 8
        for t in rendered
    )


def test_authored_unicode_outside_flash_repertoire_keeps_sparse_assets(
    target: models.Target,
) -> None:
    files: dict[int, bytes] = {}
    show = models.Setup(style=models.Style(font_name='Arimo'), fill_in_styles=False)
    display.compile_display(
        show,
        [
            display.DisplayPatch(
                values={'patch_cycle.name': 'Main', 'display_name': 'Ж'}
            )
        ],
        target,
        files,
    )
    assert files
    assert any(
        'Ж' in m.runs
        for d in files.values()
        for m in assets.decode(d, 'Arimo', 0).sizes.values()
    )


@pytest.fixture(scope='module')
def performance_renderer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    output = tmp_path_factory.mktemp('performance-renderer') / 'render'
    root = ROOT.parents[3]
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_performance_fonts.cpp'),
            '-o',
            str(output),
        ]
    )
    return native_tests.executable(output)


@pytest.mark.parametrize('name', [n for n in FONT_IDS if n not in UI_FONTS])
def test_three_performance_rows_render_with_clear_space_above_and_below(
    target: models.Target, name: str, performance_renderer: Path, tmp_path: Path
) -> None:
    face = next(f for f in target.fonts if f.name == name)
    size, metrics = next(iter(face.sizes.items()))
    characters = tmp_path / 'characters.txt'
    characters.write_text(''.join(metrics.characters), encoding='utf-8')
    native_tests.run_native(
        [
            str(performance_renderer),
            str(ROOT / 'font_sources' / f'{name}.cfont'),
            str(face.id),
            str(size),
            str(metrics.line_height),
            str(metrics.top),
            str(characters),
            str(tmp_path / f'{name}.ppm'),
        ],
        check=True,
    )
    with Image.open(tmp_path / f'{name}.ppm') as image:
        image.resize((480, 384), Image.Resampling.NEAREST).save(
            tmp_path / f'{name}.png'
        )


ROOT = Path(__file__).parents[1]
