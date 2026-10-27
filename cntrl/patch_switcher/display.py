"""Resolve named page fields and partial styles into compact display records."""

import struct
from functools import cached_property

import tomlkit
from pydantic import BaseModel, Field

from . import graphics, models


class DisplayPatch(BaseModel, frozen=True):
    values: dict[str, str | int | bool | None]
    style: models.Style | None = None
    program_style: models.Style | None = None
    categories: list[str] = Field(default_factory=list)
    program_categories: list[str] = Field(default_factory=list)

    @cached_property
    def effective_categories(self) -> list[str]:
        return list(dict.fromkeys(self.program_categories + self.categories))


def compile_display(
    show: models.Show, patches: list[DisplayPatch], target: models.Target
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
    base = models.Style(
        font_name=show.font_name,
        font_size=show.font_size,
        autoscale_fonts=show.autoscale_fonts,
    )
    requests: list[tuple[int, str, models.Style, int, int, int]] = []
    instances: list[list[int]] = []
    shared: dict[tuple[str | None, int | None, int, int], list[str]] = {}
    for patch in patches:
        category = category_style(patch.effective_categories, show.categories)
        for window in windows:
            page = show.pages[window.page]
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
                    else ', '.join(patch.effective_categories)
                    if name == 'categories'
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
                if len(text) >= 64 or any(
                    c not in target.characters or not 32 <= ord(c) <= 126 for c in text
                ):
                    raise ValueError(
                        f'field {name}: display text must contain at most '
                        '63 supported characters'
                    )
                content = name in PATCH_FIELDS
                style = merge_style(
                    base,
                    window.style,
                    page.style,
                    category if content or name == 'categories' else None,
                    patch.program_style if content else None,
                    patch.style if content else None,
                    parameter.style if parameter else None,
                    page.line_styles.get(name),
                )
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
                requests[j] = (code, text, style, pad, max(1, len(lines)), stipple)
    for _, text, style, pad, count, _ in requests:
        if not style.autoscale_fonts:
            shared.setdefault(
                (style.font_name, style.font_size, pad, count), []
            ).append(text)
    selected: dict[
        tuple[str | None, int | None, int, int], tuple[models.FontFace, int]
    ] = {}
    for key, texts in shared.items():
        font, size, pad, count = key
        selected[key] = graphics.font_choice(
            show.model_copy(update={'font_name': font, 'font_size': size}),
            texts,
            target,
            pad,
            1 if size is not None else count,
        )
    styles: list[bytes] = []
    records: list[bytes] = []
    texts: list[bytes] = []
    for code, text, style, pad, count, stipple in requests:
        face, size = (
            graphics.font_choice(
                show.model_copy(
                    update={'font_name': style.font_name, 'font_size': None}
                ),
                [text],
                target,
                pad,
                count,
            )
            if style.autoscale_fonts
            else selected[(style.font_name, style.font_size, pad, count)]
        )
        metrics = face.sizes[size]
        fg, bg = graphics.colors(style, show.contrast)
        packed = graphics.SCREEN.pack(
            face.id, size, pad, 0, fg, bg, metrics.line_height, metrics.top
        )
        if packed not in styles:
            styles.append(packed)
        left, width = graphics.bounds(text, metrics)
        x = pad + (target.display_width - 2 * pad - width) // 2 - left
        encoded = text.encode('ascii') + b'\0'
        if encoded not in texts:
            texts.append(encoded)
        records.append(
            LINE.pack(code, stipple, styles.index(packed), x, texts.index(encoded))
        )
    # Window records supply the outer background and padding, including empty panes.
    names = [str(p.values[n]) for p in patches for n in ('group.name', 'display_name')]
    window_records: list[bytes] = []
    for window in windows:
        pad = (
            window.pad
            if window.pad is not None
            else (4 if show.pad is None else show.pad)
        )
        style = merge_style(base, window.style)
        face, size = graphics.font_choice(
            show.model_copy(
                update={
                    'font_name': style.font_name,
                    'font_size': None if style.autoscale_fonts else style.font_size,
                }
            ),
            names,
            target,
            pad,
        )
        metrics = face.sizes[size]
        fg, bg = graphics.colors(style, show.contrast)
        window_records.append(
            graphics.SCREEN.pack(
                face.id, size, pad, 0, fg, bg, metrics.line_height, metrics.top
            )
        )
    pages: list[bytes] = []
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
    source['patch_categories'] = {
        str(i): p.categories for i, p in enumerate(patches) if p.categories
    }
    source['program_categories'] = {
        str(i): p.program_categories
        for i, p in enumerate(patches)
        if p.program_categories
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


def category_style(
    names: list[str], categories: dict[str, models.Category]
) -> models.Style:
    if unknown := set(names) - set(categories):
        raise ValueError(f'unknown categories: {sorted(unknown)}')
    values: dict[str, object] = {}
    priorities: dict[str, int] = {}
    owners: dict[str, str] = {}
    for name in sorted(set(names), key=lambda n: (-categories[n].priority, n)):
        category = categories[name]
        if category.style is None:
            continue
        for field, value in category.style.model_dump(exclude_none=True).items():
            if field not in priorities:
                values[field], priorities[field], owners[field] = (
                    value,
                    category.priority,
                    name,
                )
            elif priorities[field] == category.priority:
                conflict = (
                    graphics.color(str(values[field])) != graphics.color(str(value))
                    if field in ('foreground_color', 'background_color')
                    else values[field] != value
                )
                if conflict:
                    raise ValueError(
                        f'categories {owners[field]!r} and {name!r}: '
                        f'conflicting {field} '
                        f'at priority {category.priority}'
                    )
    return models.Style.model_validate(values)


def field_value(
    name: str, patch: DisplayPatch, show: models.Show
) -> str | int | bool | None:
    if name in patch.values:
        return patch.values[name]
    return getattr(show, name)


def read_source(data: bytes, offset: int) -> dict[str, object]:
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
DYNAMIC_FIELDS = (2, 3, 4, 5, 16, 17, 18, 19, 20, 21)
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
    'group.name': 0,
    'categories': CONSTANT,
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
    'font_name',
    'font_size',
    'autoscale_fonts',
    'pad',
    'contrast',
    'windows',
    'pages',
    'categories',
    'parameters',
)
