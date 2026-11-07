"""Prepare one-bit printable glyphs from outline font files."""

from pathlib import Path

from PIL import ImageFont

from .outline import Outline


def raster_data(
    path: Path, size: int, hinted: bool = False
) -> tuple[list[list[int]], list[list[int]], int]:
    if hinted:
        raster = Outline(path, autohint=True)
        glyphs = [
            raster.glyph(size, raster.face.get_char_index(c)) for c in range(32, 127)
        ]
        masks = [
            [
                int(
                    bool(g.pixels[y * ((g.width + 7) // 8) + x // 8] & (128 >> (x % 8)))
                )
                for y in range(g.height)
                for x in range(g.width)
            ]
            for g in glyphs
        ]
        cap = raster.glyph(size, raster.face.get_char_index(ord('H'))).top
        widths = [g.width for g in glyphs]
        heights = [g.height for g in glyphs]
        lefts = [g.left for g in glyphs]
        offsets = [g.top - g.height for g in glyphs]
        advances = [round(g.advance) for g in glyphs]
    else:
        font = ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.BASIC)
        cap = round(-font.getbbox('H', anchor='ls')[1])
        native = [font.getmask2(chr(c), mode='1', anchor='ls') for c in range(32, 127)]
        masks = [
            [int(bool(m[i])) for i in range(m.size[0] * m.size[1])] for m, _ in native
        ]
        advances = [round(font.getlength(chr(c))) for c in range(32, 127)]
        widths = [m.size[0] for m, _ in native]
        heights = [m.size[1] for m, _ in native]
        lefts = [o[0] for _, o in native]
        offsets = [-o[1] - m.size[1] for m, o in native]
    return masks, [widths, heights, lefts, offsets, advances], cap
