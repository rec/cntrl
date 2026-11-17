"""Shared flash fonts keep performance glyphs out of setup RAM."""

import struct
from pathlib import Path

import pytest

from cntrl.fonts import assets, ui
from cntrl.fonts.catalogue import FONT_IDS, UI_FONTS, bundled_sources
from cntrl.fonts.characters import printable_characters
from cntrl.fonts.outline import Outline
from cntrl.patch_switcher import compiler, display, graphics, models


@pytest.fixture(scope='module')
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(ROOT / 'patch-switcher-target.json')
    )


@pytest.mark.parametrize('name', list(bundled_sources()))
def test_exported_fonts_supply_every_supported_comparison_character(name: str) -> None:
    data = (ROOT / 'font_sources' / f'{name}.cfont').read_bytes()
    face = assets.decode(data, name, 0)
    outline = Outline(bundled_sources()[name])
    expected = {
        c for c in printable_characters() if outline.face.get_char_index(ord(c))
    }
    assert all(set(m.characters) == expected for m in face.sizes.values())


@pytest.mark.parametrize('name', [n for n in FONT_IDS if n not in UI_FONTS])
def test_performance_fonts_use_the_largest_comfortable_single_size(
    target: models.Target, name: str
) -> None:
    face = next(f for f in target.fonts if f.name == name)
    assert len(face.sizes) == 1
    size, metrics = next(iter(face.sizes.items()))
    assert metrics.line_height <= target.display_height // 3 - 4
    if name in bundled_sources():
        outline = Outline(bundled_sources()[name], hinting='native')
        characters = ui.supported_characters(outline)
        glyphs = [
            outline.glyph(size + 1, outline.face.get_char_index(ord(c)))
            for c in characters
        ]
        height = max(g.top for g in glyphs) + max(
            0, max(g.height - g.top for g in glyphs)
        )
        assert height > target.display_height // 3 - 4


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


ROOT = Path(__file__).parents[1]
