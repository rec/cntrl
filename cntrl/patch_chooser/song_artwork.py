"""Song captions and controller-sized RGB565 images in library order."""

import struct
from enum import IntEnum
from hashlib import sha256
from io import BytesIO
from pathlib import Path
from warnings import warn

from PIL import Image, ImageOps

from .. import source_files
from ..fonts.catalogue import bundled_sources
from ..fonts.outline import Outline
from .graphics import rgb565
from .models import Target


class TitleContrast(IntEnum):
    unchanged = 0
    black = 1
    white = 2
    black_outlined = 3
    white_outlined = 4
    authored = 5


def prepare(
    path: Path, target: Target, snapshots: dict[Path, bytes] | None, *, window: int = 1
) -> tuple[int, int, bytes]:
    screen = target.displays[
        -1 if window == 2 else min(window, len(target.displays) - 1)
    ]
    top = screen.height // 4
    bottom = screen.height * 3 // 4
    if len(target.displays) == 1:
        if window == 0:
            bottom = screen.height // 2
        else:
            top = screen.height // 2
    size = screen.width, bottom - top
    return *size, prepare_bitmap(path, size, snapshots)


def prepare_bitmap(
    path: Path, size: tuple[int, int], snapshots: dict[Path, bytes] | None
) -> bytes:
    with Image.open(BytesIO(source_files.read(path, snapshots))) as image:
        scaled = ImageOps.pad(
            image.convert('RGB'), size, method=Image.Resampling.LANCZOS
        )
    pixels = b''.join(
        struct.pack('<H', rgb565(p))
        for p in struct.iter_unpack('BBB', scaled.tobytes())
    )
    return pixels


def unpack(data: bytes) -> list[tuple[str, str, bytes]]:
    if len(data) < 4:
        raise ValueError('truncated song artwork')
    count = struct.unpack_from('<I', data)[0]
    offset = 4 + count * ENTRY.size
    if offset > len(data):
        raise ValueError('truncated song artwork directory')
    result = []
    for index in range(count):
        label, width, height, position = ENTRY.unpack_from(data, 4 + index * ENTRY.size)
        end = position + width * height * 2
        if position != offset or end > len(data) or bool(width) != bool(height):
            raise ValueError('invalid song artwork dimensions or offset')
        name = ''
        png = b''
        if width:
            name, png = png_asset(width, height, data[position:end])
        result.append((label.split(b'\0')[0].decode(), name, png))
        offset = end
    if offset != len(data):
        raise ValueError('unexpected trailing song artwork')
    return result


def png_asset(width: int, height: int, bitmap: bytes) -> tuple[str, bytes]:
    stream = BytesIO()
    Image.frombytes('RGB', (width, height), bitmap, 'raw', 'BGR;16').save(
        stream, format='PNG'
    )
    png = stream.getvalue()
    return f'images/{sha256(png).hexdigest()}.png', png


def title_contrast(
    title: str,
    width: int,
    height: int,
    bitmap: bytes,
    foreground: int | None,
    *,
    compact: bool,
) -> TitleContrast:
    font = Outline(bundled_sources()['Arimo'], hinting='auto')
    original_title = title
    prefix = title
    while True:
        for size in (16,) if compact else (20, 16):
            cursor = left = right = top = bottom = 0
            glyphs = []
            for character in title:
                glyph = font.glyph(size, font.face.get_char_index(ord(character)))
                glyphs.append((cursor + glyph.left, glyph))
                left = min(left, cursor + glyph.left)
                right = max(right, cursor + glyph.left + glyph.width)
                if glyph.width and glyph.height:
                    top = max(top, glyph.top)
                    bottom = max(bottom, glyph.height - glyph.top)
                cursor += round(glyph.advance)
            span = max(right, cursor) - left
            line_height = max(size, top + bottom)
            if span <= width - 8 and line_height <= height:
                break
        else:
            if not prefix:
                return TitleContrast.unchanged
            prefix = prefix[:-1]
            title = prefix + '...'
            continue
        break

    x = (width - span) // 2 - left
    cap = font.glyph(size, font.face.get_char_index(ord('H'))).top
    y = (height - line_height) // 2 + max(top, cap) + 2
    samples = [
        _luminance(struct.unpack_from('<H', bitmap, 2 * (row * width + col))[0])
        for offset, glyph in glyphs
        for gy in range(glyph.height)
        for gx in range(glyph.width)
        if glyph.pixels[gy * ((glyph.width + 7) // 8) + gx // 8] & (0x80 >> (gx % 8))
        and 0 <= (row := y - glyph.top + gy) < height
        and 0 <= (col := x + offset + gx) < width
    ]
    if not samples:
        return TitleContrast.unchanged

    def coverage(color: float) -> int:
        return sum(
            (max(color, s) + 0.05) / (min(color, s) + 0.05) >= 4.5 for s in samples
        )

    if foreground is not None:
        if coverage(_luminance(foreground)) < len(samples) * 0.95:
            warn(
                f'{original_title!r}: the configured title color has low contrast '
                'over the song artwork',
                stacklevel=2,
            )
        return TitleContrast.authored
    black, white = coverage(0), coverage(1)
    if max(black, white) >= len(samples) * 0.95:
        return TitleContrast.black if black >= white else TitleContrast.white
    return (
        TitleContrast.black_outlined if black >= white else TitleContrast.white_outlined
    )


def _luminance(pixel: int) -> float:
    red, green, blue = (pixel >> 11) / 31, ((pixel >> 5) & 63) / 63, (pixel & 31) / 31
    return sum(
        weight * (c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
        for c, weight in zip((red, green, blue), (0.2126, 0.7152, 0.0722), strict=True)
    )


ENTRY = struct.Struct('<64sHHI')
