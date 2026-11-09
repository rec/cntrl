"""Prepare one-bit printable glyphs from outline font files."""

from pathlib import Path

import freetype

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
        face = Outline(path).face
        face.set_pixel_sizes(size, size)
        face.load_char('H', freetype.FT_LOAD_DEFAULT)  # ty: ignore[unresolved-attribute]  # Public C extension API.
        cap = face.glyph.get_glyph().get_cbox(freetype.FT_GLYPH_BBOX_PIXELS).yMax  # ty: ignore[unresolved-attribute]  # Public C extension API.
        masks, widths, heights, lefts, offsets, advances = [], [], [], [], [], []
        for c in range(32, 127):
            face.load_char(chr(c), freetype.FT_LOAD_TARGET_MONO)  # ty: ignore[unresolved-attribute]  # Public C extension API.
            slot = face.glyph
            box = slot.get_glyph().get_cbox(freetype.FT_GLYPH_BBOX_PIXELS)  # ty: ignore[unresolved-attribute]  # Public C extension API.
            left = min(0, box.xMin)
            width = max(round(slot.advance.x / 64), box.xMax) - left
            height = max(0, box.yMax) - min(0, box.yMin)
            face.load_char(
                chr(c),
                freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO,  # ty: ignore[unresolved-attribute]  # Public C extension API.
            )
            bitmap = slot.bitmap
            pixels = bytes(bitmap.buffer)
            # Preserve the legacy canvas: ink starts at its upper edge, with
            # positive bearings retained and trailing advance space included.
            x_start = max(0, slot.bitmap_left)
            y_start = max(0, -slot.bitmap_top)
            mask = [0] * (width * height)
            for y in range(min(bitmap.rows, height - y_start)):
                for x in range(min(bitmap.width, width - x_start)):
                    mask[(y + y_start) * width + x + x_start] = int(
                        bool(pixels[y * bitmap.pitch + x // 8] & (128 >> (x % 8)))
                    )
            masks.append(mask)
            widths.append(width)
            heights.append(height)
            lefts.append(left)
            offsets.append(min(0, box.yMin))
            face.load_char(chr(c), freetype.FT_LOAD_DEFAULT)  # ty: ignore[unresolved-attribute]  # Public C extension API.
            advances.append(round(face.glyph.advance.x / 64))
    return masks, [widths, heights, lefts, offsets, advances], cap
