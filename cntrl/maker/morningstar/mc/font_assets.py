"""Compile exact show text into portable, sparse SD font assets."""

import struct
from pathlib import Path
from zlib import crc32

from ....patch_switcher import models
from .build_bitmap_fonts import FAMILIES, SIZES
from .sparse_fonts import CHARACTER, GLYPH, POSITION, RUN, MissingGlyphs, Outline


def pack(
    outline: Outline,
    selected: dict[int, list[str]],
    metrics: dict[int, models.FontMetrics] | None = None,
) -> bytes:
    entries = []
    for size, texts in sorted(selected.items()):
        runs = {t: outline.shape(size, t) for t in sorted(set(texts))}
        identifiers = sorted({p[0] for r in runs.values() for p in r.positions})
        glyphs = [outline.glyph(size, i) for i in identifiers]
        index = bytearray()
        pixels = bytearray()
        for g in glyphs:
            index.extend(
                GLYPH.pack(g.identifier, g.width, g.height, g.left, g.top, len(pixels))
            )
            pixels.extend(g.pixels)
        # Numbers change on the pedal. Reserve their individual glyphs and
        # advances; all other text is drawn using its complete prepared run.
        characters = [t for t in runs if len(t) == 1 and t in NUMBERS]
        for c in characters:
            r = runs[c]
            if len(r.positions) != 1:
                raise ValueError(f'cannot use {c!r} as a runtime numeric glyph')
            index.extend(
                CHARACTER.pack(ord(c), identifiers.index(r.positions[0][0]), r.width)
            )
        for text, run in runs.items():
            encoded = text.encode('utf-8')
            index.extend(
                RUN.pack(len(encoded), run.left, run.width, len(run.positions))
            )
            index.extend(encoded)
            for identifier, x, y in run.positions:
                index.extend(POSITION.pack(identifiers.index(identifier), x, y))
        top = min((r.top for r in runs.values()), default=0)
        bottom = max(1, max((r.bottom for r in runs.values()), default=0))
        height, baseline = (
            (metrics[size].line_height, metrics[size].top)
            if metrics
            else (max(1, bottom - top), -top)
        )
        fields = [
            size,
            2,
            0,
            0,
            0,
            0,
            0,
            *struct.pack('<HHH', len(glyphs), len(runs), len(characters)),
            height,
            baseline,
        ]
        if any(not 0 <= v <= 255 for v in fields):
            raise ValueError('font metrics exceed display limits')
        entries.append(ENTRY.pack(*fields, len(index), len(pixels)) + index + pixels)
    return HEADER.pack(b'CFN2', len(entries)) + b''.join(entries)


def entries(data: bytes) -> dict[int, tuple[list[int], bytes, bytes]]:
    if not 8 <= len(data) <= MAX_ASSET:
        raise ValueError('font asset exceeds 256 KiB or is truncated')
    magic, count = HEADER.unpack_from(data)
    if magic != b'CFN2' or not 1 <= count <= 32:
        raise ValueError('invalid font asset header')
    result = {}
    offset = HEADER.size
    for _ in range(count):
        if offset + ENTRY.size > len(data):
            raise ValueError('truncated font descriptor')
        fields = list(ENTRY.unpack_from(data, offset))
        size, ni, nd = fields[0], fields[-2], fields[-1]
        offset += ENTRY.size
        if (
            not size
            or size in result
            or fields[1:7] != [2, 0, 0, 0, 0, 0]
            or not fields[13]
            or fields[14] > 128
            or ni + nd > 96 * 1024
            or offset + ni + nd > len(data)
        ):
            raise ValueError('invalid font descriptor')
        result[size] = (
            fields,
            data[offset : offset + ni],
            data[offset + ni : offset + ni + nd],
        )
        offset += ni + nd
    if offset != len(data):
        raise ValueError('trailing font asset data')
    return result


def decode(data: bytes, name: str, key: int) -> models.FontFace:
    sizes = {}
    try:
        for size, (f, index, pixels) in entries(data).items():
            ng, nr, nc = struct.unpack('<HHH', bytes(f[7:13]))
            glyphs = []
            offset = 0
            previous = -1
            pixel_end = 0
            for _ in range(ng):
                identifier, w, h, left, top, start = GLYPH.unpack_from(index, offset)
                end = start + (w + 7) // 8 * h
                if (
                    identifier <= previous
                    or w > 160
                    or h > 128
                    or start != pixel_end
                    or end > len(pixels)
                ):
                    raise ValueError('invalid sparse glyph')
                previous, pixel_end = identifier, end
                glyphs.append((w, left))
                offset += GLYPH.size
            if pixel_end != len(pixels):
                raise ValueError('trailing font pixels')
            characters = {}
            for _ in range(nc):
                code, glyph, advance = CHARACTER.unpack_from(index, offset)
                if (
                    glyph >= ng
                    or chr(code) not in NUMBERS
                    or chr(code) in characters
                    or advance < 0
                ):
                    raise ValueError('invalid runtime character')
                w, left = glyphs[glyph]
                characters[chr(code)] = [advance, left, w]
                offset += CHARACTER.size
            runs = {}
            for _ in range(nr):
                n, left, width, count = RUN.unpack_from(index, offset)
                offset += RUN.size
                if n > 63 or offset + n + count * POSITION.size > len(index):
                    raise ValueError('truncated font run')
                text = index[offset : offset + n].decode('utf-8')
                if (
                    text in runs
                    or any(ord(c) < 32 or ord(c) == 127 for c in text)
                    or width < 0
                ):
                    raise ValueError('invalid font run')
                runs[text] = [left, width]
                offset += n
                for _ in range(count):
                    glyph, x, y = POSITION.unpack_from(index, offset)
                    if glyph >= ng or abs(x) > 32700 or abs(y) > 32700:
                        raise ValueError('invalid glyph position')
                    offset += POSITION.size
            if offset != len(index):
                raise ValueError('trailing font index data')
            sizes[size] = models.FontMetrics(
                line_height=f[13], top=f[14], runs=runs, characters=characters
            )
    except (struct.error, UnicodeError, OverflowError) as error:
        raise ValueError(f'{name}: truncated or invalid sparse font') from error
    return models.FontFace(name=name, id=255, asset=key, sizes=sizes)


class Preparation:
    def __init__(
        self,
        show: models.Show,
        target: models.Target,
        requests: list[tuple[int, str, models.Style, int, int, int]],
        contexts: list[str],
    ) -> None:
        self.outlines: dict[str, Outline] = {}
        self.data: dict[str, bytes] = {}
        self.selected: dict[str, dict[int, list[str]]] = {}
        names: dict[str, list[str]] = {}
        locations: dict[tuple[str, str], list[str]] = {}
        for (code, text, style, *_), context in zip(requests, contexts, strict=True):
            name = style.font_name or 'classic'
            texts = [text] + (
                list(NUMBERS)
                if code in NUMERIC_FIELDS
                else ['true', 'false']
                if code == 18
                else list(ACTION_STATES)
                if code == 22
                else []
            )
            names.setdefault(name, []).extend(texts)
            for t in texts:
                locations.setdefault((name, t), []).append(context)
        builtins = {n: Path(__file__).parents[3] / 'fonts' / p for n, p, _ in FAMILIES}
        faces = {f.name: f for f in target.fonts}
        errors = []
        for name, texts in names.items():
            path = Path(show.fonts[name]) if name in show.fonts else builtins.get(name)
            if path is None:
                if name not in faces:
                    errors.append(f'unknown font {name!r}')
                else:
                    for t in sorted(set(texts)):
                        missing = sorted(
                            {c for c in t if not 32 <= ord(c) <= 126}, key=ord
                        )
                        if missing:
                            errors.append(
                                f'{name}: {MissingGlyphs(missing)} '
                                f'({", ".join(dict.fromkeys(locations[name, t]))})'
                            )
                continue
            if name in show.fonts and name in faces and name not in builtins:
                errors.append(f'{name}: font name already exists')
                continue
            if path.suffix == '.cfont':
                data = path.read_bytes()
                face = decode(data, name, crc32(data))
                self.data[name] = data
                for t in set(texts):
                    if not any(
                        t in m.runs
                        or (m.characters and all(c in m.characters for c in t))
                        for m in face.sizes.values()
                    ):
                        errors.append(
                            f'{name}: text {t!r} is absent from the compiled font; '
                            f'rebuild with its original font file '
                            f'({", ".join(dict.fromkeys(locations[name, t]))})'
                        )
            else:
                outline = Outline(path)
                self.outlines[name] = outline
                valid = []
                for t in sorted(set(texts)):
                    try:
                        outline.shape(SIZES[0], t)
                        valid.append(t)
                    except ValueError as error:
                        errors.append(
                            f'{name}: {error} ({", ".join(locations[name, t])})'
                        )
                sizes = {}
                for s in SIZES:
                    runs = {t: outline.shape(s, t) for t in valid}
                    top = min((r.top for r in runs.values()), default=0)
                    bottom = max(1, max((r.bottom for r in runs.values()), default=0))
                    sizes[s] = models.FontMetrics(
                        line_height=max(1, bottom - top),
                        top=-top,
                        runs={t: [r.left, r.width] for t, r in runs.items()},
                    )
                face = models.FontFace(name=name, id=255, sizes=sizes)
            faces[name] = face
        if errors:
            raise ValueError('\n'.join(errors))
        self.target = target.model_copy(update={'fonts': list(faces.values())})
        self.show = show

    def use(self, name: str, size: int, code: int, text: str) -> None:
        if name not in self.outlines and name not in self.data:
            return
        texts = self.selected.setdefault(name, {}).setdefault(size, [])
        texts.append(text)
        texts.extend(
            list(NUMBERS)
            if code in NUMERIC_FIELDS
            else ['true', 'false']
            if code == 18
            else list(ACTION_STATES)
            if code == 22
            else []
        )

    def finish(self) -> tuple[models.Show, dict[str, int], dict[int, bytes]]:
        keys = {}
        assets = {}
        paths = {}
        for name, selected in self.selected.items():
            data = (
                self.data[name]
                if name in self.data
                else pack(
                    self.outlines[name],
                    selected,
                    next(f.sizes for f in self.target.fonts if f.name == name),
                )
            )
            key = crc32(data)
            if not key or key in assets and assets[key] != data:
                raise ValueError('font checksum collision or reserved checksum')
            decode(data, name, key)
            keys[name] = key
            assets[key] = data
            paths[name] = f'fonts/{key:08x}.cfont'
        return self.show.model_copy(update={'fonts': paths}), keys, assets


def validate_budget(
    windows: list[bytes], display: bytes, assets: dict[int, bytes]
) -> None:
    count = struct.unpack_from('<H', display, 4)[0]
    records = windows + [display[16 + i * 16 : 32 + i * 16] for i in range(count)]
    selected = {
        (struct.unpack_from('<I', r, 12)[0], r[1]) for r in records if r[0] == 255
    }
    total = sum(
        len(index) + len(pixels)
        for k, s in selected
        for _, index, pixels in [entries(assets[k])[s]]
    )
    if len(selected) > 32 or total > 96 * 1024:
        raise ValueError('selected font sizes exceed the 32-font / 96 KiB RAM budget')


HEADER = struct.Struct('<4sI')
ENTRY = struct.Struct('<15BII')
MAX_ASSET = 256 * 1024
NUMBERS = '-0123456789'
NUMERIC_FIELDS = (2, 3, 4, 5, 16, 17, 19, 20, 21)
ACTION_STATES = ('Off', 'On', 'Pending', 'Unknown / Unavailable')
