"""Compare Pillow's bundled rasterizer with the pinned FreeType library."""

from pathlib import Path

import freetype
import tyro
from PIL import ImageFont
from pydantic import BaseModel

from .build_bitmap_fonts import FAMILIES, font_sizes


class Diagnostic(BaseModel, frozen=True):
    family: str = 'RubikDistressed'


def diagnose(options: Diagnostic) -> None:
    root = Path(__file__).parents[3] / 'fonts'
    source = next(s for n, s, _ in FAMILIES if n == options.family)
    path = root / source
    face = freetype.Face.from_bytes(path.read_bytes())
    print(f'Pinned FreeType: {freetype.version()}')
    for size in font_sizes(options.family):
        face.set_pixel_sizes(size, size)
        font = ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.BASIC)
        for c in range(32, 127):
            face.load_char(
                chr(c),
                freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO,  # ty: ignore[unresolved-attribute]  # Public C extension API.
            )
            slot = face.glyph
            bitmap = slot.bitmap
            mask, origin = font.getmask2(chr(c), mode='1', anchor='ls')
            pixels = bytes(bitmap.buffer)
            differences = []
            for y in range(mask.size[1]):
                for x in range(mask.size[0]):
                    px = x - max(0, slot.bitmap_left)
                    py = y - max(0, -slot.bitmap_top)
                    native = (
                        0 <= px < bitmap.width
                        and 0 <= py < bitmap.rows
                        and bool(
                            pixels[py * bitmap.pitch + px // 8] & (128 >> (px % 8))
                        )
                    )
                    if native != bool(mask[y * mask.size[0] + x]):
                        differences.append((x, y, int(native)))
            if differences:
                print(
                    f'{options.family} {size} {chr(c)!r}: '
                    f'Pillow={mask.size, origin}, '
                    f'FT={bitmap.width, bitmap.rows}, '
                    f'bearing={slot.bitmap_left, slot.bitmap_top}, '
                    f'{len(differences)} differences: {differences[:5]}'
                )


if __name__ == '__main__':
    diagnose(tyro.cli(Diagnostic))
