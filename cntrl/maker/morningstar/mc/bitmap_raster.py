"""Prepare scaled native bitmap fonts through the same sparse asset path."""

from pathlib import Path

from .build_font_metrics import native_tables, read_bits
from .sparse_fonts import Glyph, Run


class BitmapRaster:
    def __init__(self, path: Path, name: str, horizontal_scale: float) -> None:
        self.tables = native_tables(path.read_text(), name)
        self.horizontal_scale = horizontal_scale
        self.glyphs: dict[tuple[int, int], Glyph] = {}
        self.advances: dict[tuple[int, int], float] = {}

    def glyph(self, size: int, identifier: int) -> Glyph:
        key = size, identifier
        if key in self.glyphs:
            return self.glyphs[key]
        index, data, numbers = self.tables[size]
        _, _, first, last, _, _, bi, bw, bh, bx, by, bd, _, cap = numbers
        if not first <= identifier <= last:
            raise ValueError(f'bitmap font has no U+{identifier:04X}')
        offset = read_bits(index, (identifier - first) * bi, bi) * 8
        if read_bits(data, offset, 3):
            raise ValueError('unsupported bitmap glyph encoding')
        offset += 3
        values = []
        for bits, signed in [
            (bw, False),
            (bh, False),
            (bx, True),
            (by, True),
            (bd, False),
        ]:
            values.append(read_bits(data, offset, bits, signed))
            offset += bits
        width, height, left, bottom, advance = values
        rows: list[list[int]] = []
        while len(rows) < height:
            repeated = read_bits(data, offset, 1)
            offset += 1
            count = read_bits(data, offset, 3) + 2 if repeated else 1
            offset += 3 if repeated else 0
            row = [read_bits(data, offset + x, 1) for x in range(width)]
            offset += width
            rows.extend([row] * count)
        scaled_width = max(1, round(width * self.horizontal_scale)) if width else 0
        stride = (scaled_width + 7) // 8
        pixels = bytearray(stride * height)
        for y in range(height):
            for x in range(scaled_width):
                if rows[y][min(width - 1, int(x / self.horizontal_scale))]:
                    pixels[y * stride + x // 8] |= 128 >> (x % 8)
        self.advances[key] = advance * self.horizontal_scale
        self.glyphs[key] = Glyph(
            identifier=identifier,
            width=scaled_width,
            height=height,
            left=round(left * self.horizontal_scale),
            top=height + bottom - cap,
            pixels=bytes(pixels),
        )
        return self.glyphs[key]

    def shape(self, size: int, text: str) -> Run:
        positions = []
        cursor = 0.0
        left = right = top = bottom = 0
        for char in text:
            identifier = ord(char)
            glyph = self.glyph(size, identifier)
            x = round(cursor)
            positions.append([identifier, x, 0])
            left = min(left, x + glyph.left)
            right = max(right, x + glyph.left + glyph.width)
            top = min(top, -glyph.top)
            bottom = max(bottom, glyph.height - glyph.top)
            cursor += self.advances[size, identifier]
        return Run(
            positions=positions,
            left=left,
            width=max(right, round(cursor)) - left,
            top=top,
            bottom=bottom,
        )
