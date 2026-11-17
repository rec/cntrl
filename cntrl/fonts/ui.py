"""Prepare portable embedded UI fonts without a display library or CPU SDK."""

from pathlib import Path
from typing import Literal
from zlib import crc32

import tyro
from pydantic import BaseModel, Field

from ..patch_switcher.models import FontMetrics
from .assets import decode, pack
from .catalogue import FONT_IDS, UI_FONTS, bundled_sources
from .characters import printable_characters
from .outline import Outline


class Prepare(BaseModel, frozen=True):
    """Export an outline family as a portable UI font asset."""

    name: str
    sizes: list[int] = Field(
        default_factory=lambda: list(range(8, 33)) + [40, 48, 60, 72, 96]
    )
    output: Path
    hinting: Literal['auto', 'native'] = 'auto'


def prepare(
    name: str,
    sizes: list[int],
    path: Path | None = None,
    *,
    hinting: Literal['auto', 'native'] = 'auto',
    characters: str | None = None,
    horizontal_scale: float = 1.0,
) -> bytes:
    """Use hinted monochrome glyph pixels and advances, without stretching."""
    if name not in UI_FONTS and name not in bundled_sources():
        raise ValueError(f'unknown font family {name!r}')
    if path is None:
        if name not in (sources := bundled_sources()):
            raise ValueError(f'UI font {name!r} requires an outline source')
        path = sources[name]
    outline = Outline(path, horizontal_scale, hinting=hinting)
    if characters is None:
        characters = supported_characters(outline)
    characters = ''.join(sorted(set(characters)))
    if not characters or any(
        ord(c) < 32 or 127 <= ord(c) < 160 or 0xD800 <= ord(c) <= 0xDFFF
        for c in characters
    ):
        raise ValueError('UI font characters must be printable Unicode scalars')
    selected = {s: list(characters) for s in sizes}
    metrics = {}
    for size in sizes:
        glyphs = [
            outline.glyph(size, outline.face.get_char_index(ord(c))) for c in characters
        ]
        cap = outline.glyph(size, outline.face.get_char_index(ord('H'))).top
        top = max(g.top for g in glyphs)
        bottom = max(g.height - g.top for g in glyphs)
        metrics[size] = FontMetrics(line_height=max(1, top + bottom), top=cap)
    return pack(
        outline,
        selected,
        metrics,
        runtime_characters=characters,
        hinted_advances=True,
        prepared_runs=False,
    )


def supported_characters(outline: Outline) -> str:
    return ''.join(
        c for c in printable_characters() if outline.face.get_char_index(ord(c))
    )


def performance_size(outline: Outline, characters: str, display_height: int) -> int:
    """Keep accents and descenders inside a third-height row with 2px margins."""
    height = display_height // 3 - 4

    def fits(size: int) -> bool:
        glyphs = [
            outline.glyph(size, outline.face.get_char_index(ord(c))) for c in characters
        ]
        top = max(g.top for g in glyphs)
        bottom = max(g.height - g.top for g in glyphs)
        return top + bottom <= height

    low, high = 1, 255
    selected = 0
    while low <= high:
        middle = (low + high) // 2
        if fits(middle):
            selected, low = middle, middle + 1
        else:
            high = middle - 1
    if not selected:
        raise ValueError('display is too short for a performance font')
    return selected


def embedded_header(fonts: dict[str, bytes]) -> str:
    """Emit neutral CFN2 resources; the adapter may define their storage macro."""
    if not fonts:
        raise ValueError('at least one embedded UI font is required')
    lines = [
        '// Generated portable UI font assets.',
        '#pragma once',
        '#include <cntrl/ui/fonts.h>',
        '#ifndef CNTRL_UI_FONT_STORAGE',
        '#define CNTRL_UI_FONT_STORAGE',
        '#endif',
    ]
    symbols: dict[bytes, str] = {}
    descriptors = []
    for name, data in fonts.items():
        if name not in FONT_IDS:
            raise ValueError(f'unknown font family {name!r}')
        decode(data, name, crc32(data))
        if data not in symbols:
            symbol = f'ui_font_{FONT_IDS[name]}'
            symbols[data] = symbol
            lines.append(f'inline const uint8_t {symbol}[] CNTRL_UI_FONT_STORAGE = {{')
            lines.extend(
                '    ' + ','.join(str(v) for v in data[i : i + 24]) + ','
                for i in range(0, len(data), 24)
            )
            lines.append('};')
        symbol = symbols[data]
        descriptors.append(f'    {{{FONT_IDS[name]}, {symbol}, sizeof({symbol})}},')
    lines.extend(
        [
            'inline const cntrl::ui::EmbeddedFont embedded_fonts[] '
            'CNTRL_UI_FONT_STORAGE = {',
            *descriptors,
            '};',
            '#undef CNTRL_UI_FONT_STORAGE',
            '',
        ]
    )
    return '\n'.join(lines)


def main() -> None:
    options = tyro.cli(Prepare)
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_bytes(
        prepare(options.name, options.sizes, hinting=options.hinting)
    )


if __name__ == '__main__':
    main()
