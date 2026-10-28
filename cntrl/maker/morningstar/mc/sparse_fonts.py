"""Shape show text on the host and rasterize only its selected glyphs."""

import struct
from pathlib import Path

import freetype
import uharfbuzz
from bidi import algorithm
from fontTools import unicodedata
from pydantic import BaseModel, Field


class Glyph(BaseModel, frozen=True):
    identifier: int
    width: int
    height: int
    left: int
    top: int
    pixels: bytes


class Run(BaseModel, frozen=True):
    positions: list[list[int]] = Field(default_factory=list)
    left: int = 0
    width: int = 0
    top: int = 0
    bottom: int = 0


class Outline:
    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        self.face = freetype.Face.from_bytes(self.data)
        self.hb = uharfbuzz.Font(uharfbuzz.Face(self.data))  # ty: ignore[unresolved-attribute]  # Public C extension API.
        uharfbuzz.ot_font_set_funcs(self.hb)  # ty: ignore[unresolved-attribute]  # Public C extension API.
        self.glyphs: dict[tuple[int, int], Glyph] = {}
        self.runs: dict[tuple[int, str], Run] = {}

    def glyph(self, size: int, identifier: int) -> Glyph:
        key = (size, identifier)
        if key not in self.glyphs:
            self.face.set_pixel_sizes(0, size)
            self.face.load_glyph(
                identifier,
                freetype.FT_LOAD_RENDER  # ty: ignore[unresolved-attribute]  # Public C extension API.
                | freetype.FT_LOAD_TARGET_MONO  # ty: ignore[unresolved-attribute]  # Public C extension API.
                | freetype.FT_LOAD_NO_HINTING,  # ty: ignore[unresolved-attribute]  # Public C extension API.
            )
            slot = self.face.glyph
            bitmap = slot.bitmap
            stride = (bitmap.width + 7) // 8
            source = bytes(bitmap.buffer)
            pixels = b''.join(
                source[y * bitmap.pitch : y * bitmap.pitch + stride]
                for y in range(bitmap.rows)
            )
            self.glyphs[key] = Glyph(
                identifier=identifier,
                width=bitmap.width,
                height=bitmap.rows,
                left=slot.bitmap_left,
                top=slot.bitmap_top,
                pixels=pixels,
            )
        return self.glyphs[key]

    def shape(self, size: int, text: str) -> Run:
        key = (size, text)
        if key in self.runs:
            return self.runs[key]
        self.hb.scale = (size * 64, size * 64)
        positions: list[list[int]] = []
        x = y = 0
        left = right = top = bottom = 0
        missing: set[str] = set()
        for start, end, level, script in text_runs(text):
            buffer = uharfbuzz.Buffer()  # ty: ignore[unresolved-attribute]  # Public C extension API.
            buffer.add_codepoints([ord(c) for c in text[start:end]])
            buffer.guess_segment_properties()
            buffer.direction = 'rtl' if level % 2 else 'ltr'
            buffer.script = script
            buffer.language = 'und'
            uharfbuzz.shape(self.hb, buffer)  # ty: ignore[unresolved-attribute]  # Public C extension API.
            info, offsets = buffer.glyph_infos, buffer.glyph_positions
            assert offsets is not None
            for item in info:
                if not item.codepoint:
                    boundaries = sorted({i.cluster for i in info} | {end - start})
                    stop = next(i for i in boundaries if i > item.cluster)
                    missing.update(
                        c
                        for c in text[start + item.cluster : start + stop]
                        if unicodedata.category(c) != 'Cf'
                    )
            for item, offset in zip(info, offsets, strict=True):
                if not item.codepoint:
                    continue
                glyph = self.glyph(size, item.codepoint)
                px, py = (
                    round((x + offset.x_offset) / 64),
                    round(-(y + offset.y_offset) / 64),
                )
                positions.append([item.codepoint, px, py])
                left = min(left, px + glyph.left)
                right = max(right, px + glyph.left + glyph.width)
                top = min(top, py - glyph.top)
                bottom = max(bottom, py - glyph.top + glyph.height)
                x += offset.x_advance
                y += offset.y_advance
        if missing:
            raise MissingGlyphs(sorted(missing, key=ord))
        right = max(right, round(x / 64))
        result = Run(
            positions=positions, left=left, width=right - left, top=top, bottom=bottom
        )
        self.runs[key] = result
        return result


class MissingGlyphs(ValueError):
    def __init__(self, characters: list[str]) -> None:
        self.characters = characters
        super().__init__(', '.join(f'U+{ord(c):04X} {c!r}' for c in characters))


def text_runs(text: str) -> list[tuple[int, int, int, str]]:
    """Itemize logical script runs, then put them in bidi visual order."""
    if not text:
        return []
    if any(unicodedata.bidirectional(c) in ('LRI', 'RLI', 'FSI', 'PDI') for c in text):
        raise ValueError(
            'explicit bidi isolation controls are not supported in display labels'
        )
    storage = algorithm.get_empty_storage()
    storage['base_level'] = algorithm.get_base_level(text)
    storage['base_dir'] = 'R' if storage['base_level'] else 'L'
    algorithm.get_embedding_levels(text, storage)
    for i, c in enumerate(storage['chars']):
        c['index'] = i
    algorithm.explicit_embed_and_overrides(storage)
    algorithm.resolve_weak_types(storage)
    algorithm.resolve_neutral_types(storage, False)
    algorithm.resolve_implicit_levels(storage, False)
    # Common characters inherit the preceding script, then the following one
    # at the start. Joining controls stay within the original logical slices.
    scripts = [unicodedata.script(c) for c in text]
    previous = 'Zyyy'
    for i, s in enumerate(scripts):
        if s not in ('Zyyy', 'Zinh', 'Zzzz'):
            previous = s
        else:
            scripts[i] = previous
    following = 'Latn'
    for i in range(len(scripts) - 1, -1, -1):
        if scripts[i] not in ('Zyyy', 'Zinh', 'Zzzz'):
            following = scripts[i]
        else:
            scripts[i] = following
    groups: list[tuple[int, int, int, str]] = []
    indices: dict[int, int] = {}
    for c in storage['chars']:
        i, level = c['index'], c['level']
        if groups and groups[-1][2:] == (level, scripts[i]):
            start, _, _, script = groups[-1]
            groups[-1] = (start, i + 1, level, script)
        else:
            groups.append((i, i + 1, level, scripts[i]))
        indices[i] = len(groups) - 1
    if not groups:
        return []
    # Include ignored shaping controls at run edges too.
    groups[0] = (0, *groups[0][1:])
    groups = [
        (g[0], groups[i + 1][0] if i + 1 < len(groups) else len(text), g[2], g[3])
        for i, g in enumerate(groups)
    ]
    algorithm.reorder_resolved_levels(storage, False)
    order = list(dict.fromkeys(indices[c['index']] for c in storage['chars']))
    return [groups[i] for i in order]


# A size entry contains sparse glyph descriptors, a runtime character map,
# exact UTF-8 strings and their prepared glyph positions, then bitmap bytes.
GLYPH = struct.Struct('<IHHhhI')
CHARACTER = struct.Struct('<IHh')
RUN = struct.Struct('<HhhH')
POSITION = struct.Struct('<Hhh')
