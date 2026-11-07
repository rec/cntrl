"""Prepare portable embedded UI fonts without a display library or CPU SDK."""

from pathlib import Path
from zlib import crc32

from ..patch_switcher.models import FontMetrics
from .assets import decode, pack
from .catalogue import UI_FONTS, bundled_sources
from .outline import Outline


def prepare(name: str, sizes: list[int], path: Path | None = None) -> bytes:
    """Use hinted monochrome glyph pixels and advances, without stretching."""
    if name not in UI_FONTS:
        raise ValueError(f'unknown UI font family {name!r}')
    if path is None:
        if name not in (sources := bundled_sources()):
            raise ValueError(f'UI font {name!r} requires an outline source')
        path = sources[name]
    outline = Outline(path, autohint=True)
    characters = ''.join(chr(c) for c in range(32, 127))
    selected = {s: list(characters) for s in sizes}
    metrics = {}
    for size in sizes:
        glyphs = [
            outline.glyph(size, outline.face.get_char_index(c)) for c in range(32, 127)
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
        if name not in UI_FONTS:
            raise ValueError(f'unknown UI font family {name!r}')
        decode(data, name, crc32(data))
        if data not in symbols:
            symbol = f'ui_font_{UI_FONTS[name]}'
            symbols[data] = symbol
            lines.append(f'inline const uint8_t {symbol}[] CNTRL_UI_FONT_STORAGE = {{')
            lines.extend(
                '    ' + ','.join(str(v) for v in data[i : i + 24]) + ','
                for i in range(0, len(data), 24)
            )
            lines.append('};')
        symbol = symbols[data]
        descriptors.append(f'    {{{UI_FONTS[name]}, {symbol}, sizeof({symbol})}},')
    lines.extend(
        [
            'inline const cntrl::ui::EmbeddedFont embedded_fonts[] = {',
            *descriptors,
            '};',
            '#undef CNTRL_UI_FONT_STORAGE',
            '',
        ]
    )
    return '\n'.join(lines)
