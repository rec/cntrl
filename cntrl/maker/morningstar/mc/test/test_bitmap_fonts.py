"""Verify the firmware glyph encoding against FreeType's source rasterization."""

import re
from pathlib import Path

import pytest
from PIL import ImageFont

from cntrl.maker.morningstar.mc import build_bitmap_fonts, build_font_metrics
from cntrl.maker.morningstar.mc.build_font_metrics import read_bits
from cntrl.patch_switcher import compiler, models


@pytest.mark.parametrize('family', build_bitmap_fonts.FAMILIES)
@pytest.mark.parametrize('size', build_bitmap_fonts.SIZES)
def test_firmware_glyphs_preserve_pixels_and_baseline(
    family: tuple[str, str, str], size: int
) -> None:
    name, source, _ = family
    root = Path(__file__).parents[1]
    outline = root.parents[2] / 'fonts' / source
    generated = build_bitmap_fonts.bitmap_font(outline, name, size)
    arrays = re.findall(
        r'unsigned char \w+\[\] CNTRL_FONT_FLASH = \{(.*?)\};', generated, re.S
    )
    index, data = (
        bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]+)', a)) for a in arrays
    )
    raw = generated.rsplit('font_t ', 1)[1].split('{', 1)[1].split('}', 1)[0]
    fields = [f.strip() for f in raw.split(',') if f.strip()]
    bi, bw, bh, bx, by, bd, _, cap = (int(f) for f in fields[9:])
    font = ImageFont.truetype(outline, size, layout_engine=ImageFont.Layout.BASIC)
    for c in range(32, 127):
        offset = read_bits(index, (c - 32) * bi, bi) * 8
        assert read_bits(data, offset, 3) == 0
        offset += 3
        metrics = []
        for b, signed in [
            (bw, False),
            (bh, False),
            (bx, True),
            (by, True),
            (bd, False),
        ]:
            metrics.append(read_bits(data, offset, b, signed))
            offset += b
        width, height, left, y, advance = metrics
        mask, origin = font.getmask2(chr(c), mode='1', anchor='ls')
        assert (width, height) == mask.size
        assert (left, cap - height - y) == (origin[0], cap + origin[1])
        assert advance == round(font.getlength(chr(c)))
        pixels = []
        for _ in range(height):
            assert read_bits(data, offset, 1) == 0
            offset += 1
            pixels.extend(read_bits(data, offset + x, 1) for x in range(width))
            offset += width
        assert pixels == [int(bool(p)) for p in mask]


def test_generated_fonts_and_exported_metrics_are_current(tmp_path: Path) -> None:
    root = Path(__file__).parents[1]
    build_bitmap_fonts.generate(tmp_path)
    target = models.Target.model_validate(
        compiler.read_document(root / 'patch-switcher-target.json')
    )
    for p in tmp_path.iterdir():
        assert (
            p.read_bytes() == (root / 'firmware/patch_switcher' / p.name).read_bytes()
        )
    for i, (n, _, _) in enumerate(build_bitmap_fonts.FAMILIES, 3):
        face = build_font_metrics.native_face(
            (tmp_path / f'font_{n}.cpp').read_text(), n, i
        )
        assert face == next(f for f in target.fonts if f.name == n)
