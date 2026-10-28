"""Check exported widths against the bundled native fonts' index format."""

from pathlib import Path

import pytest

from cntrl.maker.morningstar.mc import build_font_metrics
from cntrl.patch_switcher import compiler, display, graphics, models


def test_page_lines_select_different_native_font_families() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'patch-switcher-target.json')
    )
    show = models.Show.model_validate(
        {
            'instruments': {},
            'groups': {},
            'style': {'font_name': 'Arial'},
            'pages': {
                'patch': {'line_styles': {'display_name': {'font_name': 'ComicSansMS'}}}
            },
        }
    )
    value = display.DisplayPatch(
        values={'group.name': 'Brass', 'display_name': 'Trumpet'}
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
