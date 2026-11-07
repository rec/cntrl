"""Scale portable bitmap font assets using the same shaped-run interface."""

import struct
from pathlib import Path

from .outline import CHARACTER, GLYPH, Glyph, Run


class BitmapRaster:
    def __init__(self, path: Path, horizontal_scale: float = 1.0) -> None:
        from .assets import decode, entries

        data = path.read_bytes()
        decode(data, path.stem, 0)
        self.tables = entries(data)
        self.horizontal_scale = horizontal_scale
        self.glyphs: dict[tuple[int, int], Glyph] = {}
        self.characters: dict[int, dict[int, tuple[int, int]]] = {}
        for size, (fields, index, _) in self.tables.items():
            ng, _, nc = struct.unpack('<HHH', bytes(fields[7:13]))
            self.characters[size] = {}
            for i in range(nc):
                code, glyph, advance = CHARACTER.unpack_from(
                    index, ng * GLYPH.size + i * CHARACTER.size
                )
                self.characters[size][code] = glyph, advance

    def glyph(self, size: int, identifier: int) -> Glyph:
        key = size, identifier
        if key in self.glyphs:
            return self.glyphs[key]
        if identifier not in self.characters[size]:
            raise ValueError(f'bitmap font has no U+{identifier:04X}')
        glyph, advance = self.characters[size][identifier]
        _, index, data = self.tables[size]
        _, width, height, left, top, offset = GLYPH.unpack_from(
            index, glyph * GLYPH.size
        )
        scaled_width = max(1, round(width * self.horizontal_scale)) if width else 0
        stride = (scaled_width + 7) // 8
        pixels = bytearray(stride * height)
        for y in range(height):
            for x in range(scaled_width):
                sx = min(width - 1, int(x / self.horizontal_scale))
                if data[offset + y * ((width + 7) // 8) + sx // 8] & (128 >> (sx % 8)):
                    pixels[y * stride + x // 8] |= 128 >> (x % 8)
        result = Glyph(
            identifier=identifier,
            width=scaled_width,
            height=height,
            left=round(left * self.horizontal_scale),
            top=top,
            pixels=bytes(pixels),
            advance=advance * self.horizontal_scale,
        )
        self.glyphs[key] = result
        return result

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
            cursor += glyph.advance
        return Run(
            positions=positions,
            left=left,
            width=max(right, round(cursor)) - left,
            top=top,
            bottom=bottom,
        )
