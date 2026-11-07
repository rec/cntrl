"""Check exported widths against the bundled native fonts' index format."""

from pathlib import Path

import pytest

from cntrl.maker.morningstar.mc import build_font_metrics
from cntrl.maker.morningstar.mc.native_raster import NativeRaster
from cntrl.patch_switcher import compiler, display, graphics, models


def test_page_lines_select_different_native_font_families() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'patch-switcher-target.json')
    )
    show = models.Setup.model_validate(
        {
            'instruments': {},
            'patch_cycles': {},
            'style': {'font_name': 'Arial'},
            'patch_views': {
                'patch': {'line_styles': {'display_name': {'font_name': 'ComicSansMS'}}}
            },
        }
    )
    value = display.DisplayPatch(
        values={'patch_cycle.name': 'Brass', 'display_name': 'Trumpet'}
    )
    _, _, block = display.compile_display(show, [value], target)
    assert graphics.SCREEN.unpack_from(block, display.HEADER.size)[0] == 1
    assert (
        graphics.SCREEN.unpack_from(block, display.HEADER.size + graphics.SCREEN.size)[
            0
        ]
        == 2
    )


@pytest.mark.parametrize(
    'name',
    [
        'Arial',
        'ComicSansMS',
        'Circuit',
        'CircuitBold',
        'StillTime',
        'MetalLord',
        'CaveatBold',
        'CourierPrime',
        'CourierPrimeBold',
        'DecoRound',
        'DMSerifDisplay',
        'CollegeBlock',
        'OrnamentalSlab',
        'RubikDistressed',
        'SpecialElite',
    ],
)
def test_native_autoscale_uses_largest_fitting_size_per_label(name: str) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'patch-switcher-target.json')
    )
    face = next(f for f in target.fonts if f.name == name)
    texts = ['I', 'Wide patch name']
    for t in texts:
        _, size = graphics.font_choice(name, [t], target, 4)
        metrics = face.sizes[size]
        left, width = graphics.bounds(t, metrics)
        assert width <= 152 and metrics.line_height <= 59
        assert left <= 0
        assert all(
            m.line_height > 59 or graphics.bounds(t, m)[1] > 152
            for s, m in face.sizes.items()
            if s > size
        )


def test_native_metrics_are_proportional_and_fit_selected_sizes() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'patch-switcher-target.json')
    )
    for name in ('Arial', 'ComicSansMS'):
        face = next(f for f in target.fonts if f.name == name)
        assert (
            graphics.bounds('WWWW', face.sizes[16])[1]
            > graphics.bounds('iiii', face.sizes[16])[1]
        )
        names = ['Trumpet', 'Maynard', 'trombone', 'Flute']
        _, size = graphics.font_choice(name, names, target, 4)
        assert all(graphics.bounds(t, face.sizes[size])[1] <= 152 for t in names)
        assert all(
            2 * m.line_height + 2 > 120
            or any(graphics.bounds(t, m)[1] > 152 for t in names)
            for s, m in face.sizes.items()
            if s > size
        )


def test_classic_uses_native_arial_metrics_instead_of_enlarged_pixels() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'patch-switcher-target.json')
    )
    classic = next(f for f in target.fonts if f.name == 'classic')
    arial = next(f for f in target.fonts if f.name == 'Arial')
    assert classic.id == 0 and classic.sizes == arial.sizes
    assert 56 not in classic.sizes
    assert (
        graphics.bounds('WWWW', classic.sizes[32])[1]
        > graphics.bounds('iiii', classic.sizes[32])[1]
    )


def test_bit_reader_crosses_bytes_and_decodes_signed_offsets() -> None:
    assert build_font_metrics.read_bits(bytes.fromhex('ab80'), 4, 8) == 184
    assert build_font_metrics.read_bits(bytes.fromhex('f0'), 0, 4, True) == -1


def test_unknown_font_is_rejected() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'patch-switcher-target.json')
    )
    with pytest.raises(ValueError, match='unknown font'):
        graphics.font_choice(
            'absent',
            ['Flute'],
            target,
            4,
        )


@pytest.mark.parametrize('family', ['Arial', 'ComicSansMS'])
def test_bitmap_scaling_doubles_columns_without_changing_rows(family: str) -> None:
    source = build_font_metrics.Options().source / f'st7735_t3_font_{family}.c'
    normal = NativeRaster(source, family, 1.0)
    wide = NativeRaster(source, family, 2.0)
    for letter in 'AgW':
        first, second = normal.glyph(24, ord(letter)), wide.glyph(24, ord(letter))
        assert second.width == 2 * first.width
        assert (second.height, second.top) == (first.height, first.top)
        assert second.left == 2 * first.left
        for y in range(first.height):
            for x in range(first.width):
                expected = bool(
                    first.pixels[y * ((first.width + 7) // 8) + x // 8]
                    & (128 >> (x % 8))
                )
                for column in (2 * x, 2 * x + 1):
                    actual = bool(
                        second.pixels[y * ((second.width + 7) // 8) + column // 8]
                        & (128 >> (column % 8))
                    )
                    assert actual == expected


@pytest.mark.parametrize('family', ['Arial', 'ComicSansMS'])
def test_exported_native_resources_preserve_scaled_glyphs_and_runs(family: str) -> None:
    from cntrl.fonts.bitmap import BitmapRaster

    source = build_font_metrics.Options().source / f'st7735_t3_font_{family}.c'
    resource = Path(__file__).parents[1] / 'font_sources' / f'{family}.cfont'
    for scale in (1.0, 1.25, 2.0):
        original = NativeRaster(source, family, scale)
        portable = BitmapRaster(resource, scale)
        for size in original.tables:
            for code in range(32, 127):
                assert portable.glyph(size, code) == original.glyph(size, code)
            assert portable.shape(size, 'Wide patch 123') == original.shape(
                size, 'Wide patch 123'
            )
