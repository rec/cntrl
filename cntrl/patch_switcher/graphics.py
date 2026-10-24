"""Resolve show graphics and precompute text placement, without device access."""

import math
import re
import struct
import warnings

from . import models


def color(value: str) -> tuple[int, int, int]:
    value = COLORS.get(value.lower(), value)
    if not re.fullmatch(r'0x[0-9a-fA-F]{6}', value):
        raise ValueError(f'unknown color {value!r}: use a named color or 0xRRGGBB')
    number = int(value[2:], 16)
    return number >> 16, (number >> 8) & 255, number & 255


def distance(first: tuple[int, int, int], second: tuple[int, int, int]) -> float:
    return (
        math.sqrt(sum((a - b) ** 2 for a, b in zip(first, second, strict=True)) / 3)
        / 255
    )


def colors(screen: models.ScreenGraphics, contrast: float) -> tuple[int, int, int]:
    foreground, background = screen.foreground_color, screen.background_color
    if foreground is None and background is None:
        foreground, background = 'black', 'white'
    if foreground is None or background is None:
        chosen = color(foreground if foreground is not None else str(background))
        complement = [255 - c for c in chosen]
        # Integer RGB space has only 511 distinct uniform brightness shifts.
        # Enumerating them is exact, bounded and occurs only during compilation.
        candidates = [
            tuple(max(0, min(255, c + t)) for c in complement) for t in range(-255, 256)
        ]
        candidates = [(c[0], c[1], c[2]) for c in candidates]
        possible = [
            (abs(t - 255), c)
            for t, c in enumerate(candidates)
            if distance(chosen, c) >= contrast
        ]
        other = (
            min(possible)[1]
            if possible
            else tuple(0 if c >= 128 else 255 for c in chosen)
        )
        other = (other[0], other[1], other[2])
        fg, bg = (chosen, other) if foreground is not None else (other, chosen)
    else:
        fg, bg = color(foreground), color(background)
    actual = distance(fg, bg)
    if actual <= contrast:
        warnings.warn(
            f'foreground/background distance {actual:.3f} <= contrast {contrast:.3f}',
            UserWarning,
            stacklevel=2,
        )
    half = (
        color(screen.half)
        if screen.half
        else tuple((a + b) // 2 for a, b in zip(fg, bg, strict=True))
    )
    return rgb565(fg), rgb565(bg), rgb565((half[0], half[1], half[2]))


def rgb565(value: tuple[int, int, int]) -> int:
    return (value[0] >> 3) << 11 | (value[1] >> 2) << 5 | value[2] >> 3


def bounds(text: str, metrics: models.FontMetrics) -> tuple[int, int]:
    cursor = 0
    left, right = 0, 0
    for c in text:
        index = (ord(c) - 32) * 3
        advance, bearing, width = metrics.data[index : index + 3]
        if bearing >= 128:
            bearing -= 256
        left = min(left, cursor + bearing)
        right = max(right, cursor + bearing + width)
        cursor += advance
    return left, max(cursor, right) - left


def font_choice(
    show: models.Show,
    texts: list[str],
    target: models.Target,
    pad: int,
) -> tuple[models.FontFace, int]:
    face = next(
        (
            f
            for f in target.fonts
            if f.name == ('classic' if show.font_name is None else show.font_name)
        ),
        None,
    )
    if face is None:
        raise ValueError(f'unknown font {show.font_name!r}')
    width, height = target.display_width - 2 * pad, target.display_height - 2 * pad
    if min(width, height) <= 0:
        raise ValueError('padding leaves no drawable display area')
    sizes = sorted(face.sizes, reverse=True)
    if show.font_size is not None:
        if show.font_size not in sizes:
            raise ValueError(f'{face.name}: font_size must be one of {sorted(sizes)}')
        sizes = [show.font_size]

    def fits(size: int) -> bool:
        metrics = face.sizes[size]
        widest = sorted((bounds(t, metrics)[1] for t in texts), reverse=True)[:4]
        return 2 * metrics.line_height + 2 <= height and max(widest, default=0) <= width

    # Refine the largest-size estimate using the four widest rendered strings.
    candidate = sizes[0]
    while not fits(candidate):
        maximum = max((bounds(t, face.sizes[candidate])[1] for t in texts), default=1)
        estimate = min(candidate - 1, candidate * width // max(1, maximum))
        smaller = [s for s in sizes if s <= estimate]
        if not smaller:
            break
        candidate = smaller[0]
    # Discrete font sizes/rounding need a final check, including skipped sizes.
    selected = next((s for s in sizes if s >= candidate and fits(s)), None)
    if selected is None:
        selected = next((s for s in sizes if fits(s)), None)
    if selected is None:
        raise ValueError(
            f'{face.name}: category and patch names do not fit the padded display'
        )
    return face, selected


def compile_screens(
    show: models.Show, texts: list[str], target: models.Target
) -> tuple[list[bytes], list[list[int]]]:
    screens = show.screens or models.Screens()
    entries = [
        getattr(screens, n) or models.ScreenGraphics() for n in ('undo', 'sent', 'next')
    ]
    pad = 4 if show.pad is None else show.pad
    errors: list[str] = []
    pairs: list[tuple[int, int, int]] = []
    if 2 * pad >= min(target.display_width, target.display_height):
        errors.append('padding leaves no drawable display area')
    for name, screen in zip(('undo', 'sent', 'next'), entries, strict=True):
        try:
            pairs.append(colors(screen, show.contrast))
        except ValueError as error:
            errors.append(f'screens.{name}: {error}')
    if errors:
        raise ValueError('\n'.join(errors))
    face, size = font_choice(show, texts, target, pad)
    metrics = face.sizes[size]
    records: list[bytes] = []
    positions: list[list[int]] = []
    for fg, bg, half in pairs:
        width = target.display_width - 2 * pad
        records.append(
            SCREEN.pack(
                face.id, size, pad, 0, fg, bg, metrics.line_height, metrics.top, half, 0
            )
        )
        positions.append(
            [
                pad + (width - w) // 2 - left
                for left, w in (bounds(t, metrics) for t in texts)
            ]
        )
    return records, positions


SCREEN = struct.Struct('<BBBBHHHHHH')
COLORS = {
    'black': '0x000000',
    'white': '0xFFFFFF',
    'red': '0xFF0000',
    'green': '0x008000',
    'lime': '0x00FF00',
    'blue': '0x0000FF',
    'yellow': '0xFFFF00',
    'cyan': '0x00FFFF',
    'aqua': '0x00FFFF',
    'magenta': '0xFF00FF',
    'fuchsia': '0xFF00FF',
    'silver': '0xC0C0C0',
    'gray': '0x808080',
    'grey': '0x808080',
    'maroon': '0x800000',
    'olive': '0x808000',
    'purple': '0x800080',
    'teal': '0x008080',
    'navy': '0x000080',
    'orange': '0xFFA500',
}
