"""Resolve named page fields and partial styles into compact display records."""

import struct
from functools import cached_property

import tomlkit
from pydantic import BaseModel, Field

from . import graphics, models


class DisplayPatch(BaseModel, frozen=True):
    context: str = 'patch'
    values: dict[str, str | int | bool | None]
    style: models.Style | None = None
    program_style: models.Style | None = None
    tags: list[str] = Field(default_factory=list)
    program_tags: list[str] = Field(default_factory=list)

    @cached_property
    def effective_tags(self) -> list[str]:
        return list(dict.fromkeys(self.program_tags + self.tags))


def compile_display(
    show: models.Show,
    patches: list[DisplayPatch],
    target: models.Target,
    assets: dict[int, bytes] | None = None,
    patch_metadata: dict[str, object] | None = None,
) -> tuple[list[bytes], list[list[int]], bytes]:
    overlap = set(show.parameters) & set(FIELDS)
    if overlap:
        raise ValueError(f'duplicate field definitions: {sorted(overlap)}')
    for name, page in show.pages.items():
        if len(page.fields) > MAX_LINES:
            raise ValueError(f'pages.{name}: at most {MAX_LINES} fields are supported')
        unknown = set(page.fields) - set(FIELDS) - set(show.parameters)
        if unknown:
            raise ValueError(f'pages.{name}: unknown fields {sorted(unknown)}')
    windows = [getattr(show.windows, n) for n in WINDOW_NAMES]
    errors: list[str] = []
    for name, window in zip(WINDOW_NAMES, windows, strict=True):
        if window.page not in show.pages:
            raise ValueError(f'windows.{name}: unknown page {window.page!r}')
        try:
            if window.style:
                for key in ('foreground_color', 'background_color'):
                    if value := getattr(window.style, key):
                        graphics.color(value)
        except ValueError as error:
            errors.append(f'windows.{name}.style: {error}')
    if errors:
        raise ValueError('\n'.join(errors))
    base = show.style
    requests: list[tuple[int, str, models.Style, int, int, int]] = []
    instances: list[list[int]] = []
    contexts: list[str] = []
    for patch in patches:
        tag = tag_style(patch.program_tags + patch.tags, show.tags)
        for window in windows:
            page = (
                show.pages.get(
                    'switches',
                    models.Page(fields=['switch.name', 'switch.mode', 'switch.state']),
                )
                if 'switch.name' in patch.values
                else show.pages[window.page]
            )
            if 'switch.name' in patch.values and (
                len(page.fields) > 3
                or any(
                    not n.startswith('switch.')
                    and n not in show.parameters
                    and FIELDS.get(n) not in range(16, 22)
                    for n in page.fields
                )
            ):
                raise ValueError(
                    'switch pages support at most three action or parameter fields'
                )
            pad = (
                window.pad
                if window.pad is not None
                else (4 if show.pad is None else show.pad)
            )
            if 2 * pad >= min(target.display_width, target.display_height):
                raise ValueError('padding leaves no drawable display area')
            lines: list[int] = []
            for name in page.fields:
                parameter = show.parameters.get(name)
                code = FIELDS.get(name, CONSTANT)
                value = (
                    parameter.value
                    if parameter
                    else ', '.join(patch.effective_tags)
                    if name == 'tags'
                    else field_value(name, patch, show)
                )
                if (value is None or value == '') and code not in DYNAMIC_FIELDS:
                    continue
                text = (
                    '-'
                    if value is None
                    else str(value).lower()
                    if isinstance(value, bool)
                    else str(value)
                )
                if len(text.encode('utf-8')) >= 64 or any(
                    ord(c) < 32 or ord(c) == 127 for c in text
                ):
                    raise ValueError(
                        f'field {name}: display text must contain at most '
                        '63 UTF-8 bytes and no control characters'
                    )
                content = name in PATCH_FIELDS or name.startswith('switch.')
                style = merge_style(
                    base,
                    window.style,
                    page.style,
                    tag if content or name == 'tags' else None,
                    patch.program_style if content else None,
                    patch.style if content else None,
                    parameter.style if parameter else None,
                    page.line_styles.get(name),
                )
                contexts.append(f'{patch.context}, field {name}')
                lines.append(len(requests))
                requests.append(
                    (
                        code,
                        text,
                        style,
                        pad,
                        max(1, len(page.fields)),
                        int(name == 'display_name'),
                    )
                )
            instances.append(lines)
            for j in lines:
                code, text, style, pad, _, stipple = requests[j]
                requests[j] = (
                    code,
                    text,
                    style,
                    pad,
                    max(6 if 'switch.name' in patch.values else 1, len(lines)),
                    stipple,
                )
    from ..maker.morningstar.mc.font_assets import SWITCH_STATES, Preparation

    prepared = Preparation(show, target, requests, contexts)
    target = prepared.target
    choices = [
        graphics.font_choice(
            s.font_name, list(SWITCH_STATES) if c == 22 else [t], target, p, n
        )
        for c, t, s, p, n, _ in requests
    ]
    for (code, text, _, _, _, _), (face, size) in zip(requests, choices, strict=True):
        prepared.use(face.name, size, code, text)
    show, keys, generated = prepared.finish()
    if assets is not None:
        assets.update(generated)
    styles: list[bytes] = []
    records: list[bytes] = []
    texts: list[bytes] = []
    for (code, text, style, pad, _count, stipple), (face, size) in zip(
        requests, choices, strict=True
    ):
        metrics = face.sizes[size]
        fg, bg = graphics.colors(style, show.contrast)
        packed = graphics.SCREEN.pack(
            face.id, size, pad, 0, fg, bg, metrics.line_height, metrics.top
        )
        packed = packed[:12] + struct.pack('<I', keys.get(face.name, face.asset))
        if packed not in styles:
            styles.append(packed)
        left, width = graphics.bounds(text, metrics)
        x = pad + (target.display_width - 2 * pad - width) // 2 - left
        encoded = text.encode('utf-8') + b'\0'
        if encoded not in texts:
            texts.append(encoded)
        records.append(
            LINE.pack(code, stipple, styles.index(packed), x, texts.index(encoded))
        )
    # Window records supply the outer background and padding, including empty panes.
    window_records: list[bytes] = []
    for window in windows:
        pad = (
            window.pad
            if window.pad is not None
            else (4 if show.pad is None else show.pad)
        )
        style = merge_style(base, window.style)
        face = next(f for f in target.fonts if f.name == 'classic')
        size = 8
        metrics = face.sizes[size]
        fg, bg = graphics.colors(style, show.contrast)
        window_records.append(
            graphics.SCREEN.pack(
                face.id, size, pad, 0, fg, bg, metrics.line_height, metrics.top
            )[:12]
            + struct.pack('<I', keys.get(face.name, face.asset))
        )
    pages: list[bytes] = [] if patches else [b'\0']
    references: list[list[int]] = []
    for i in range(len(patches)):
        refs: list[int] = []
        for slot in range(3):
            indexes = instances[i * 3 + slot]
            page = bytes([len(indexes)]) + b''.join(records[j] for j in indexes)
            heights = sum(
                struct.unpack_from('<H', styles[LINE.unpack(records[j])[2]], 8)[0]
                for j in indexes
            )
            if heights > target.display_height - 2 * (
                windows[slot].pad
                if windows[slot].pad is not None
                else (4 if show.pad is None else show.pad)
            ):
                raise ValueError(
                    f'windows.{WINDOW_NAMES[slot]}: page text exceeds drawable height'
                )
            if page not in pages:
                pages.append(page)
            refs.append(pages.index(page))
        references.append(refs)
    source = {
        k: v
        for k, v in show.model_dump(exclude_none=True).items()
        if k in SOURCE_FIELDS
    }
    source['switches'] = {
        k: {
            n: w
            for n, w in models.SwitchAssignment.model_validate(v)
            .model_dump(exclude_none=True)
            .items()
            if n in ('name', 'style')
        }
        for k, v in show.switches.items()
    }
    source['patch_metadata'] = patch_metadata or {}
    if show.fonts:
        source['fonts'] = show.fonts
    source['patch_styles'] = {
        str(i): p.style.model_dump(exclude_none=True)
        for i, p in enumerate(patches)
        if p.style is not None
    }
    source['program_styles'] = {
        str(i): p.program_style.model_dump(exclude_none=True)
        for i, p in enumerate(patches)
        if p.program_style is not None
    }
    source['patch_tags'] = {str(i): p.tags for i, p in enumerate(patches) if p.tags}
    source['program_tags'] = {
        str(i): p.program_tags for i, p in enumerate(patches) if p.program_tags
    }
    authored = tomlkit.dumps(source).encode('utf-8')
    text_data = b''.join(struct.pack('<H', len(t)) + t for t in texts)
    page_data = b''.join(pages)
    if len(page_data) > 65535 or len(styles) > 65535 or len(texts) > 65535:
        raise ValueError('display metadata exceeds storage capacity')
    block = HEADER.pack(
        b'CDSP', len(styles), len(pages), len(texts), len(page_data), len(authored)
    )
    block += b''.join(styles) + page_data + text_data + authored
    if len(block) > 65535:
        raise ValueError('display metadata exceeds 65535 bytes')
    return window_records, references, block


def merge_style(*styles: models.Style | None) -> models.Style:
    values: dict[str, object] = {}
    for style in styles:
        if style is not None:
            values.update(style.model_dump(exclude_none=True))
    return models.Style.model_validate(values)


def tag_style(names: list[str], tags: dict[str, models.Tag]) -> models.Style:
    return merge_style(*(tags[n].style for n in names if n in tags))


def field_value(
    name: str, patch: DisplayPatch, show: models.Show
) -> str | int | bool | None:
    if name in patch.values:
        return patch.values[name]
    return getattr(show, name)


def read_source(data: bytes, offset: int) -> dict[str, object]:
    if data[offset : offset + 4] != b'CPAT' or offset + 16 > len(data):
        raise ValueError('missing patch graph')
    offset += struct.unpack_from('<I', data, offset + 4)[0]
    if offset + HEADER.size > len(data):
        raise ValueError('truncated display metadata')
    magic, styles, pages, texts, page_size, source_size = HEADER.unpack_from(
        data, offset
    )
    if magic != b'CDSP' or not pages:
        raise ValueError('invalid display metadata')
    offset += HEADER.size + styles * graphics.SCREEN.size + page_size
    for _ in range(texts):
        if offset + 2 > len(data):
            raise ValueError('truncated display text')
        size = struct.unpack_from('<H', data, offset)[0]
        offset += 2 + size
    if offset + source_size != len(data):
        raise ValueError('invalid display source size')
    return tomlkit.parse(data[offset:].decode('utf-8')).unwrap()


MAX_LINES = 16
CONSTANT = 255
DYNAMIC_FIELDS = (2, 3, 4, 5, 16, 17, 18, 19, 20, 21, 22)
WINDOW_NAMES = ('undo', 'sent', 'next')
LINE = struct.Struct('<BBHHH')
HEADER = struct.Struct('<4sHHHHI')
PATCH_FIELDS = (
    'display_name',
    'name',
    'program',
    'bank',
    'volume',
    'pan',
    'instrument',
)
FIELDS = {
    'switch.name': 255,
    'switch.mode': 255,
    'switch.button': 255,
    'switch.state': 22,
    'patch_list.name': 0,
    'tags': CONSTANT,
    'display_name': 1,
    'program': 2,
    'bank': 3,
    'volume': 4,
    'pan': 5,
    'instrument': 6,
    'name': 7,
    'brightness': 16,
    'splash': 17,
    'usb_midi_interface': 18,
    'screensaver_timeout': 19,
    'chord_ms': 20,
    'transition_ms': 21,
}
SOURCE_FIELDS = (
    'switches',
    'style',
    'pad',
    'contrast',
    'windows',
    'pages',
    'tags',
    'parameters',
)
