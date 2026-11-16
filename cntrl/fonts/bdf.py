"""Preserve native BDF pixels and advances in portable ASCII UI assets."""

from ..patch_switcher.models import FontMetrics
from .assets import pack
from .outline import Glyph, Run


class Bdf:
    def __init__(self, data: bytes) -> None:
        self.glyphs: dict[int, Glyph] = {}
        for block in data.decode().split('STARTCHAR ')[1:]:
            header, bitmap = block.split('\nBITMAP\n', 1)
            fields = {s.split()[0]: s.split()[1:] for s in header.splitlines()[1:]}
            code = int(fields['ENCODING'][0])
            if not 32 <= code < 127:
                continue
            width, height, left, bottom = map(int, fields['BBX'])
            pixels = bytes.fromhex(bitmap.split('ENDCHAR', 1)[0])
            if len(pixels) != ((width + 7) // 8) * height:
                raise ValueError(f'invalid BDF bitmap for U+{code:04X}')
            self.glyphs[code] = Glyph(
                identifier=code,
                width=width,
                height=height,
                left=left,
                top=bottom + height,
                pixels=pixels,
                advance=int(fields['DWIDTH'][0]),
            )
        if set(self.glyphs) != set(range(32, 127)):
            missing = sorted(set(range(32, 127)) - self.glyphs.keys())
            raise ValueError(f'BDF UI font missing ASCII codes: {missing}')

    def glyph(self, size: int, identifier: int) -> Glyph:
        return self.glyphs[identifier]

    def shape(self, size: int, text: str) -> Run:
        positions: list[list[int]] = []
        cursor = left = right = top = bottom = 0
        for c in text:
            g = self.glyphs[ord(c)]
            positions.append([ord(c), cursor, 0])
            left = min(left, cursor + g.left)
            right = max(right, cursor + g.left + g.width)
            top = min(top, -g.top)
            bottom = max(bottom, g.height - g.top)
            cursor += round(g.advance)
        return Run(
            positions=positions,
            left=left,
            width=max(right, cursor) - left,
            top=top,
            bottom=bottom,
        )


def prepare(data: bytes, size: int) -> bytes:
    font = Bdf(data)
    characters = ''.join(chr(c) for c in range(32, 127))
    return pack(
        font,
        {size: list(characters)},
        {size: FontMetrics(line_height=size, top=font.glyph(size, ord('H')).top)},
        runtime_characters=characters,
        hinted_advances=True,
        prepared_runs=False,
    )
