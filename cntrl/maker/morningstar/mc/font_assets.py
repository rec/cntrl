"""Prepare portable SD font assets using the existing display font encoding."""

import struct
from pathlib import Path
from zlib import crc32

from ....patch_switcher import models
from .build_bitmap_fonts import SIZES, bitmap_data
from .build_font_metrics import read_bits


def encode(path: Path) -> bytes:
    if path.suffix == '.cfont':
        return path.read_bytes()
    entries = []
    for s in SIZES:
        index, data, descriptor = bitmap_data(path, s)
        if any(not 0 <= n <= 255 for n in descriptor):
            raise ValueError(f'{path}: font metrics exceed the bitmap format limits')
        entries.append(ENTRY.pack(s, *descriptor, len(index), len(data)) + index + data)
    return HEADER.pack(b'CFN1', len(entries)) + b''.join(entries)


def decode(data: bytes, name: str, key: int) -> models.FontFace:
    if not 8 <= len(data) <= MAX_ASSET:
        raise ValueError(f'{name}: font asset exceeds 256 KiB or is truncated')
    magic, count = HEADER.unpack_from(data)
    if magic != b'CFN1' or not 1 <= count <= 32:
        raise ValueError(f'{name}: invalid font asset header')
    sizes: dict[int, models.FontMetrics] = {}
    offset = HEADER.size
    for _ in range(count):
        if offset + ENTRY.size > len(data):
            raise ValueError(f'{name}: truncated font descriptor')
        size, *fields, ni, nd = ENTRY.unpack_from(data, offset)
        offset += ENTRY.size
        if (
            size in sizes
            or not size
            or fields[:6] != [1, 0, 32, 126, 0, 0]
            or any(not 1 <= b <= 16 for b in fields[6:12])
            or not fields[12]
            or fields[13] > 128
            or ni != (95 * fields[6] + 7) // 8
            or offset + ni + nd > len(data)
            or ni + nd > 96 * 1024
            or not nd
        ):
            raise ValueError(f'{name}: invalid font descriptor')
        index, pixels = data[offset : offset + ni], data[offset + ni : offset + ni + nd]
        glyphs = bytearray()
        top, bottom = 0, fields[12]
        try:
            for c in range(95):
                bit = read_bits(index, c * fields[6], fields[6]) * 8
                if read_bits(pixels, bit, 3):
                    raise ValueError('unsupported glyph encoding')
                bit += 3
                values = []
                for b, signed in zip(
                    fields[7:12], [False, False, True, True, False], strict=True
                ):
                    values.append(read_bits(pixels, bit, b, signed))
                    bit += b
                width, height, left, y, advance = values
                if (
                    width > 160
                    or height > 128
                    or not -128 <= left <= 127
                    or advance > 255
                ):
                    raise ValueError('glyph exceeds display limits')
                for _ in range(height):
                    if read_bits(pixels, bit, 1):
                        raise ValueError('unsupported row encoding')
                    bit += 1 + width
                if bit > len(pixels) * 8:
                    raise ValueError('truncated glyph')
                top = min(top, fields[13] - height - y)
                bottom = max(bottom, fields[13] - y)
                glyphs.extend([advance, left & 255, width])
        except IndexError as error:
            raise ValueError(f'{name}: truncated glyph') from error
        sizes[size] = models.FontMetrics(
            line_height=bottom - top, top=-top, glyphs=glyphs.hex()
        )
        offset += ni + nd
    if offset != len(data):
        raise ValueError(f'{name}: trailing font asset data')
    return models.FontFace(name=name, id=255, asset=key, sizes=sizes)


def prepare_fonts(
    show: models.Show, target: models.Target
) -> tuple[models.Show, models.Target, dict[int, bytes]]:
    faces = list(target.fonts)
    assets: dict[int, bytes] = {}
    paths: dict[str, str] = {}
    for n, p in show.fonts.items():
        if any(f.name == n for f in faces):
            raise ValueError(f'{n}: font name already exists')
        data = encode(Path(p))
        key = crc32(data)
        if not key:
            raise ValueError(f'{n}: reserved asset checksum')
        if key in assets and assets[key] != data:
            raise ValueError('font checksum collision')
        faces.append(decode(data, n, key))
        assets[key] = data
        paths[n] = f'fonts/{key:08x}.cfont'
    return (
        show.model_copy(update={'fonts': paths}),
        target.model_copy(update={'fonts': faces}),
        assets,
    )


def validate_budget(
    windows: list[bytes], display: bytes, assets: dict[int, bytes]
) -> None:
    count = struct.unpack_from('<H', display, 4)[0]
    records = windows + [display[16 + i * 16 : 32 + i * 16] for i in range(count)]
    selected = {
        (struct.unpack_from('<I', r, 12)[0], r[1]) for r in records if r[0] == 255
    }
    total = 0
    for k, s in selected:
        data = assets[k]
        offset = HEADER.size
        for _ in range(HEADER.unpack_from(data)[1]):
            size, *_, ni, nd = ENTRY.unpack_from(data, offset)
            if size == s:
                total += ni + nd
            offset += ENTRY.size + ni + nd
    if len(selected) > 32 or total > 96 * 1024:
        raise ValueError('selected font sizes exceed the 32-font / 96 KiB RAM budget')


HEADER = struct.Struct('<4sI')
ENTRY = struct.Struct('<15BII')
MAX_ASSET = 256 * 1024
