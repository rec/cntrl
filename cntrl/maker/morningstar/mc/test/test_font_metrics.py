"""Check exported widths against the bundled native fonts' index format."""

import struct
from pathlib import Path

import pytest

from cntrl.maker.morningstar.mc import build_font_metrics
from cntrl.patch_switcher import compiler, graphics, models


@pytest.mark.parametrize('name', ['Arial', 'ComicSansMS'])
def test_native_autoscale_uses_largest_fitting_size_per_label(name: str) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'patch-switcher-target.json')
    )
    face = next(f for f in target.fonts if f.name == name)
    texts = ['I', 'Wide patch name']
    _, offsets, layouts = graphics.compile_screens(
        models.Show(instruments={}, patches={}, font_name=name, autoscale_fonts=True),
        texts,
        target,
        1,
    )
    for i, t in enumerate(texts):
        size, _, top, height, y = struct.unpack_from('<BBHHH', layouts, 4 + i * 8)
        metrics = face.sizes[size]
        left, width = graphics.bounds(t, metrics)
        assert width <= 152 and height <= 59
        assert top == metrics.top and height == metrics.line_height
        assert offsets[0][i] + left == 4 + (152 - width) // 2
        assert y == (4 if i == 0 else 65)
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
        records, offsets, _ = graphics.compile_screens(
            models.Show(instruments={}, patches={}, font_name=name), names, target
        )
        fields = graphics.SCREEN.unpack(records[0])
        size = fields[1]
        assert all(graphics.bounds(t, face.sizes[size])[1] <= 152 for t in names)
        assert all(x >= 4 for x in offsets[0])
        assert all(
            2 * m.line_height + 2 > 120
            or any(graphics.bounds(t, m)[1] > 152 for t in names)
            for s, m in face.sizes.items()
            if s > size
        )


def test_bit_reader_crosses_bytes_and_decodes_signed_offsets() -> None:
    assert build_font_metrics.read_bits(bytes.fromhex('ab80'), 4, 8) == 184
    assert build_font_metrics.read_bits(bytes.fromhex('f0'), 0, 4, True) == -1


def test_missing_font_size_is_rejected() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'patch-switcher-target.json')
    )
    with pytest.raises(ValueError, match='font_size must be one of'):
        graphics.compile_screens(
            models.Show(instruments={}, patches={}, font_name='Arial', font_size=15),
            ['Flute'],
            target,
        )
