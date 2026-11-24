"""Resolve named page fields and partial styles into compact display records."""

import struct
from functools import cached_property
from pathlib import Path
from random import Random
from typing import cast

import tomlkit
from pydantic import BaseModel, Field
from tomlkit.exceptions import ParseError

from ..fonts.catalogue import UI_FONTS
from . import formatting, graphics, models


class DisplayPatch(BaseModel, frozen=True):
    context: str = 'patch'
    values: dict[str, str | int | bool | None]
    style: models.Style | None = None
    autostyle: tuple[str, int] | None = None
    program_style: models.Style | None = None
    tags: list[str] = Field(default_factory=list)
    program_tags: list[str] = Field(default_factory=list)
    live_cycle_name: bool = False

    @cached_property
    def effective_tags(self) -> list[str]:
        return list(dict.fromkeys(self.program_tags + self.tags))


def compile_display(
    show: models.Setup,
    patches: list[DisplayPatch],
    target: models.Target,
    assets: dict[int, bytes] | None = None,
    patch_metadata: dict[str, object] | None = None,
    source_snapshots: dict[Path, bytes] | None = None,
    include_artwork: bool = True,
    binding_images: list[str] | None = None,
) -> tuple[list[bytes], list[list[int]], bytes]:
    if show.fill_in_styles and show.seed is None:
        # Persist the one-time choice so downloaded configurations recompile to
        # the same appearance, even when the original input requested randomness.
        show = show.model_copy(update={'seed': Random().getrandbits(63)})
    cycles = formatting.styles(
        show,
        (
            (n, show.patch_cycle_seeds.get(n))
            for p in patches
            if isinstance(n := p.values.get('patch_cycle.name'), str) and n
        ),
        target,
        'cycle',
    )
    patch_styles = formatting.styles(
        show, (p.autostyle for p in patches if p.autostyle is not None), target, 'patch'
    )
    named_fields = {k for p in patches for k in p.values if k.startswith('names.')}
    counter_fields = {f'counters.{n}' for n in show.counters}
    overlap = set(show.parameters) & (set(FIELDS) | named_fields | counter_fields)
    if overlap:
        raise ValueError(f'duplicate field definitions: {sorted(overlap)}')
    for name, page in show.patch_views.items():
        if len(page.fields) > MAX_LINES:
            raise ValueError(
                f'patch_views.{name}: at most {MAX_LINES} fields are supported'
            )
        unknown = (
            set(page.fields)
            - set(FIELDS)
            - named_fields
            - set(show.parameters)
            - counter_fields
        )
        if unknown:
            raise ValueError(f'patch_views.{name}: unknown fields {sorted(unknown)}')
    displays = [getattr(show.displays, n) for n in DISPLAY_NAMES]
    errors: list[str] = []
    for name, display in zip(DISPLAY_NAMES, displays, strict=True):
        if display.view not in show.patch_views:
            raise ValueError(f'displays.{name}: unknown patch view {display.view!r}')
        try:
            if display.style:
                for key in ('foreground_color', 'background_color'):
                    if value := getattr(display.style, key):
                        graphics.color(value)
        except ValueError as error:
            errors.append(f'displays.{name}.style: {error}')
    if errors:
        raise ValueError('\n'.join(errors))
    base = show.style
    requests: list[tuple[int, str, models.Style, int, int, int]] = []
    counter_names: dict[int, str] = {}
    counter_texts: dict[int, list[str]] = {}
    instances: list[list[int]] = []
    contexts: list[str] = []
    paired_lines: list[list[int]] = []
    for patch in patches:
        cycle = str(patch.values.get('patch_cycle.name') or '')
        cycle_style = cycles.get((cycle, show.patch_cycle_seeds.get(cycle)))
        patch_style = (
            patch_styles.get(patch.autostyle, cycle_style)
            if patch.autostyle is not None
            else cycle_style
        )
        tag = tag_style(patch.program_tags + patch.tags, show.tag_styles)
        for display in displays:
            page = (
                show.patch_views.get(
                    'switches',
                    models.PatchView(
                        fields=['switch.name', 'switch.mode', 'switch.state']
                    ),
                )
                if 'switch.name' in patch.values
                else show.patch_views[display.view]
            )
            if 'switch.name' in patch.values and (
                len(page.fields) > 3
                or any(
                    not n.startswith('switch.')
                    and n not in show.parameters
                    and n not in counter_fields
                    and FIELDS.get(n) not in range(16, 22)
                    for n in page.fields
                )
            ):
                raise ValueError(
                    'switch pages support at most three action or parameter fields'
                )
            pad = (
                display.pad
                if display.pad is not None
                else (4 if show.pad is None else show.pad)
            )
            if 2 * pad >= min(target.display_width, target.display_height):
                raise ValueError('padding leaves no drawable display area')
            lines: list[int] = []
            for name in page.fields:
                parameter = show.parameters.get(name)
                code = 24 if name in counter_fields else FIELDS.get(name, CONSTANT)
                if code == 0 and patch.live_cycle_name:
                    code = 25
                value = (
                    show.counters[name[9:]].initial
                    if code == 24
                    else parameter.value
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
                    else format(value, 'g')
                    if code in (20, 21, 23) and isinstance(value, float)
                    else str(value)
                )
                if len(text.encode('utf-8')) >= 64 or any(
                    ord(c) < 32 or ord(c) == 127 for c in text
                ):
                    raise ValueError(
                        f'field {name}: display text must contain at most '
                        '63 UTF-8 bytes and no control characters'
                    )
                content = name in PATCH_FIELDS or name.startswith(('switch.', 'names.'))
                style = merge_style(
                    patch_style
                    if content or name == 'tags'
                    else cycle_style
                    if name == 'patch_cycle.name'
                    else None,
                    base,
                    display.style,
                    page.style,
                    tag if content or name == 'tags' else None,
                    patch.program_style if content else None,
                    patch.style if content else None,
                    parameter.style if parameter else None,
                    page.line_styles.get(name),
                )
                contexts.append(f'{patch.context}, field {name}')
                lines.append(len(requests))
                if code == 24:
                    counter_names[len(requests)] = name[9:]
                    counter = show.counters[name[9:]]
                    counter_texts[len(requests)] = [
                        str(i) for i in range(counter.minimum, counter.maximum + 1)
                    ]
                requests.append(
                    (
                        code,
                        text,
                        style,
                        pad,
                        max(1, len(page.fields)),
                        int(name == 'display_name' or name.startswith('names.')),
                    )
                )
            instances.append(lines)
            if len(lines) >= 3 and 'switch.name' not in patch.values:
                paired_lines.append(lines[1:3])
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
    from ..fonts.assets import SWITCH_STATES, Preparation

    display_face = next(f for f in target.fonts if f.name == 'Arimo')
    prepared = Preparation(show, target, requests, contexts, source_snapshots)
    target = prepared.target
    choices = [
        graphics.font_choice(
            name,
            counter_texts.get(i, list(SWITCH_STATES) if c == 22 else [t]),
            target,
            p,
            n,
        )
        for i, (name, (c, t, s, p, n, _)) in enumerate(
            zip(prepared.names, requests, strict=True)
        )
    ]
    for indexes in paired_lines:
        indexes = [i for i in indexes if len(choices[i][0].sizes) > 1]
        if len(indexes) < 2:
            continue
        limit = min(choices[i][1] for i in indexes)
        sizes = set.intersection(*(set(choices[i][0].sizes) for i in indexes))
        sizes = {
            s
            for s in sizes
            if s <= limit
            and all(
                not choices[i][0].sizes[s].runs
                or requests[i][1] in choices[i][0].sizes[s].runs
                for i in indexes
            )
        }
        if not sizes:
            raise ValueError(
                'second and third patch lines have no shared fitting font size'
            )
        for i in indexes:
            choices[i] = choices[i][0], max(sizes)
    for i, (face, size) in enumerate(choices):
        if (
            len(face.sizes) != 1
            or face.id == 255
            or face.name.split('@', 1)[0] in target.ui_fonts
        ):
            continue
        code, text, style, pad, count, stipple = requests[i]
        metrics = face.sizes[size]
        width = target.display_width - 2 * pad
        if graphics.bounds(text, metrics)[1] > width:
            suffix = '…' if '…' in metrics.characters else '...'
            prefix = text
            while prefix and graphics.bounds(prefix + suffix, metrics)[1] > width:
                prefix = prefix[:-1]
            if graphics.bounds(prefix + suffix, metrics)[1] > width:
                raise ValueError(f'{face.name}: ellipsis does not fit the display')
            requests[i] = code, prefix + suffix, style, pad, count, stipple
    for (code, text, _, _, _, _), (face, size) in zip(requests, choices, strict=True):
        prepared.use(face.name, size, code, text)
    show, keys, generated = prepared.finish()
    if assets is not None:
        assets.update(generated)
    styles: list[tuple[bytes, str]] = []
    records: list[bytes] = []
    texts: list[bytes] = []
    for index, ((code, text, style, pad, _count, stipple), (face, size)) in enumerate(
        zip(requests, choices, strict=True)
    ):
        metrics = face.sizes[size]
        fg, bg = graphics.colors(style, show.contrast)
        packed = graphics.SCREEN.pack(
            face.id, size, pad, 0, fg, bg, metrics.line_height, metrics.top
        )
        packed = packed[:12] + struct.pack('<I', keys.get(face.name, face.asset))
        key = packed, style.image or ''
        if key not in styles:
            styles.append(key)
        left, width = graphics.bounds(text, metrics)
        x = pad + (target.display_width - 2 * pad - width) // 2 - left
        encoded = counter_names.get(index, text).encode('utf-8') + b'\0'
        if encoded not in texts:
            texts.append(encoded)
        records.append(
            LINE.pack(code, stipple, styles.index(key), x, texts.index(encoded))
        )
    # Display records supply the outer background and padding, including empty panes.
    display_records: list[bytes] = []
    for display in displays:
        pad = (
            display.pad
            if display.pad is not None
            else (4 if show.pad is None else show.pad)
        )
        style = merge_style(base, display.style)
        face = display_face
        size = 8
        metrics = face.sizes[size]
        fg, bg = graphics.colors(style, show.contrast)
        display_records.append(
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
                struct.unpack_from('<H', styles[LINE.unpack(records[j])[2]][0], 8)[0]
                for j in indexes
            )
            if heights > target.display_height - 2 * (
                displays[slot].pad
                if displays[slot].pad is not None
                else (4 if show.pad is None else show.pad)
            ):
                raise ValueError(
                    f'displays.{DISPLAY_NAMES[slot]}: page text exceeds drawable height'
                )
            if page not in pages:
                pages.append(page)
            refs.append(pages.index(page))
        references.append(refs)
    source = {
        k: v
        for k, v in show.model_dump(exclude_none=True).items()
        if k in SOURCE_FIELDS and (k not in ('bindings', 'patch_cycle_seeds') or v)
    }
    if show.pickup_pairing_ms == 2:
        source.pop('pickup_pairing_ms', None)
    if 'fast_switches' in show.model_fields_set:
        source['fast_switches'] = show.fast_switches
    if not show.inline_patches:
        source['inline_patches'] = False
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
    if contact := show.page_settings.contact_style:
        font = 253
        size = 16
        if name := contact.font_name:
            font = UI_FONTS.get(name, 0)
            size = max(
                (
                    s
                    for f in target.fonts
                    if f.id == font
                    for s in f.sizes
                    if 8 <= s <= 16
                ),
                default=0,
            )
            if name not in target.ui_fonts or not size:
                raise ValueError(
                    f'page_settings.contact_style: unavailable UI font {name!r}'
                )
        foreground = graphics.rgb565(
            graphics.color(contact.foreground_color or 'black')
        )
        background = graphics.rgb565(
            graphics.color(
                contact.background_color or show.page_settings.background_color
            )
        )
        styles.append(
            (
                graphics.SCREEN.pack(
                    font, size, 0, CONTACT_STYLE, foreground, background, size, 0
                ),
                contact.image or '',
            )
        )
        source['contact_style'] = contact.model_dump(exclude_none=True)
    images = (binding_images or []) + [n if include_artwork else '' for _, n in styles]
    if contact and contact.image:
        images[-1] = contact.image
    if any(images):
        source['artwork_sources'] = images
    authored = tomlkit.dumps(source).encode('utf-8')
    text_data = b''.join(struct.pack('<H', len(t)) + t for t in texts)
    page_data = b''.join(pages)
    if len(page_data) > 65535 or len(styles) > 65535 or len(texts) > 65535:
        raise ValueError('display metadata exceeds storage capacity')
    block = HEADER.pack(
        b'CDSP', len(styles), len(pages), len(texts), len(page_data), len(authored)
    )
    block += b''.join(s for s, _ in styles) + page_data + text_data + authored
    if any(images):
        from .song_artwork import prepare_bitmap

        if len(images) > 65535:
            raise ValueError('artwork directory exceeds 65535 entries')

        width = target.display_width
        height = target.display_height
        page_settings = show.page_settings
        contact_height = max(
            1,
            height
            - min(
                page_settings.title_height * 3 // 2,
                (height - 2 * page_settings.padding) // 2,
            )
            - 6,
        )
        directory = bytearray(
            b'CART' + struct.pack('<II', len(images), len(binding_images or []))
        )
        pixels = bytearray()
        offsets: dict[tuple[str, int], int] = {}
        start = 12 + 8 * len(images)
        for i, name in enumerate(images):
            if not name:
                directory.extend(bytes(8))
                continue
            image_height = (
                contact_height
                if contact and i == len(images) - 1
                else (height + 2) // 3
            )
            key = name, image_height
            if key not in offsets:
                offsets[key] = start + len(pixels)
                pixels.extend(
                    prepare_bitmap(Path(name), (width, image_height), source_snapshots)
                )
            directory.extend(struct.pack('<HHI', width, image_height, offsets[key]))
        block += (
            directory
            + pixels
            + struct.pack('<4sI', b'AEND', len(directory) + len(pixels) + 8)
        )
    return display_records, references, block


def artwork_files(
    data: bytes, source: dict[str, object]
) -> tuple[dict[str, str], dict[Path, bytes]]:
    from . import binary_format
    from .song_artwork import png_asset

    data = binary_format.core(data)
    if data[-8:-4] != b'AEND':
        return {}, {}
    length = struct.unpack_from('<I', data, len(data) - 4)[0]
    start = len(data) - length
    if start < 0 or length < 20 or data[start : start + 4] != b'CART':
        raise binary_format.DocumentFormatError('invalid setup artwork directory')
    count = struct.unpack_from('<I', data, start + 4)[0]
    paths = source.get('artwork_sources')
    if not isinstance(paths, list) or len(paths) != count or count > (length - 20) // 8:
        raise binary_format.DocumentFormatError('invalid setup artwork source paths')
    names: dict[str, str] = {}
    files: dict[Path, bytes] = {}
    contact = source.get('contact_style')
    for i, original in enumerate(paths):
        width, height, offset = struct.unpack_from('<HHI', data, start + 12 + i * 8)
        if not offset:
            continue
        end = offset + width * height * 2
        if (
            not isinstance(original, str)
            or not original
            or not width
            or not height
            or offset < 12 + count * 8
            or end > length - 8
        ):
            raise binary_format.DocumentFormatError('invalid setup artwork entry')
        name, png = png_asset(width, height, data[start + offset : start + end])
        key = (
            f'contact:{original}'
            if i == count - 1 and isinstance(contact, dict) and contact.get('image')
            else original
        )
        names[key] = name
        files[Path(name)] = png
    return names, files


def remap_artwork(
    value: object, names: dict[str, str], *, contact: bool = False
) -> None:
    if isinstance(value, dict):
        fields = cast(dict[str, object], value)
        if isinstance(image := fields.get('image'), str):
            key = f'contact:{image}' if contact else image
            if key in names:
                fields['image'] = names[key]
        for key, item in fields.items():
            remap_artwork(item, names, contact=key == 'contact_style')
    elif isinstance(value, list):
        for item in value:
            remap_artwork(item, names)


def estimate_frame_ram(setup: models.Setup, target: models.Target) -> int:
    """Count checked full-frame allocations and one-frame transition snapshots."""
    pixels = 2 * sum(d.width * d.height for d in target.displays)
    total = setup.frame_buffers * (((pixels + 7) & ~7) + 8)
    if setup.frame_buffers == 1 and setup.use_animations:
        # ARM Pane is 1976 bytes; Renderer checks this against sizeof(Pane).
        total += ((len(target.displays) * 1976 + 7) & ~7) + 8
    return total


def estimate_font_ram(
    displays: list[bytes], block: bytes, fonts: dict[int, bytes]
) -> int:
    from ..fonts import assets

    styles = HEADER.unpack_from(block)[1]
    records = displays + [
        block[
            HEADER.size + i * graphics.SCREEN.size : HEADER.size
            + (i + 1) * graphics.SCREEN.size
        ]
        for i in range(styles)
    ]
    selected = {(struct.unpack_from('<I', r, 12)[0], r[1]) for r in records}
    selected = {(k, s) for k, s in selected if k}
    if not selected:
        return 0
    # ARM Loaded is 36 bytes. Each exact allocation uses the same alignment
    # and header admission model as MemoryBuffer; target tracing reports actuals.
    total = ((len(selected) * 36 + 7) & ~7) + 8
    decoded = {k: assets.entries(fonts[k]) for k in {k for k, _ in selected}}
    for k, s in selected:
        _, index, pixels = decoded[k][s]
        total += ((len(index) + len(pixels) + 7) & ~7) + 8
    return total


def merge_style(*styles: models.Style | None) -> models.Style:
    values: dict[str, object] = {}
    for style in styles:
        if style is not None:
            values.update(style.model_dump(exclude_none=True))
    return models.Style.model_validate(values)


def tag_style(names: list[str], tags: dict[str, models.Style]) -> models.Style:
    return merge_style(*(tags[n] for n in names if n in tags))


def field_value(
    name: str, patch: DisplayPatch, show: models.Setup
) -> str | int | float | bool | None:
    if name == 'splash_movie':
        return bool(show.splash_movie)
    if name == 'navigation.hold_delay':
        return show.navigation.hold_delay
    if name.startswith('scrolling.'):
        return getattr(show.scrolling, name.split('.')[1])
    if name in patch.values:
        return patch.values[name]
    if name.startswith('names.'):
        return None
    return getattr(show, name)


def read_source(data: bytes, offset: int) -> dict[str, object]:
    from . import binary_format

    if data.startswith(binary_format.MAGIC):
        offset -= binary_format.core_offset(data)
        data = binary_format.core(data)
    if data[offset : offset + 4] != b'CPAT' or offset + 16 > len(data):
        raise binary_format.DocumentFormatError('missing patch graph')
    offset += struct.unpack_from('<I', data, offset + 4)[0]
    if offset + HEADER.size > len(data):
        raise binary_format.DocumentFormatError('truncated display metadata')
    magic, styles, pages, texts, page_size, source_size = HEADER.unpack_from(
        data, offset
    )
    if magic != b'CDSP' or not pages:
        raise binary_format.DocumentFormatError('invalid display metadata')
    offset += HEADER.size + styles * graphics.SCREEN.size + page_size
    for _ in range(texts):
        if offset + 2 > len(data):
            raise binary_format.DocumentFormatError('truncated display text')
        size = struct.unpack_from('<H', data, offset)[0]
        offset += 2 + size
    end = offset + source_size
    if end > len(data) or (end < len(data) and data[end : end + 4] != b'CART'):
        raise binary_format.DocumentFormatError('invalid display source size')
    try:
        source = tomlkit.parse(data[offset:end].decode('utf-8')).unwrap()
        # Older compiled setups retain the authoring keys used at compile time.
        if 'windows' in source:
            source['displays'] = source.pop('windows')
        for page in source.get('pages', {}).values():
            if isinstance(page, dict):
                if 'min_windows' in page:
                    page['min_displays'] = page.pop('min_windows')
                if 'give_up_windows' in page:
                    page['give_up_displays'] = page.pop('give_up_windows')
        stack = [source]
        while stack:
            fields = stack.pop()
            if isinstance(fields, dict):
                for key, value in fields.items():
                    if key in ('style', 'status_style') and isinstance(value, dict):
                        value.pop('icon', None)
                    if key in (
                        'line_styles',
                        'tag_styles',
                        'patch_styles',
                        'program_styles',
                    ) and isinstance(value, dict):
                        for item in value.values():
                            if isinstance(item, dict):
                                item.pop('icon', None)
                    stack.append(value)
            elif isinstance(fields, list):
                stack.extend(fields)
        return source
    except (UnicodeDecodeError, ParseError) as error:
        raise binary_format.DocumentFormatError(
            f'invalid display source: {error}'
        ) from error


MAX_LINES = 16
CONSTANT = 255
DYNAMIC_FIELDS = (2, 3, 4, 5, 16, 17, 18, 19, 20, 21, 22, 23, 24)
DISPLAY_NAMES = ('next', 'sent', 'undo')
LINE = struct.Struct('<BBHHH')
HEADER = struct.Struct('<4sHHHHI')
CONTACT_STYLE = 0x80
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
    'patch_cycle.name': 0,
    'tags': CONSTANT,
    'display_name': 1,
    'program': 2,
    'bank': 3,
    'volume': 4,
    'pan': 5,
    'instrument': 6,
    'name': 7,
    'brightness': 16,
    'splash_movie': 17,
    'is_midi_interface': 18,
    'screensaver_timeout': 19,
    'navigation.hold_delay': 20,
    'scrolling.horizontal_duration': 21,
    'scrolling.vertical_duration': 23,
}
SOURCE_FIELDS = (
    'bindings',
    'pickup',
    'pickup_pairing_ms',
    'midi',
    'splash_movie',
    'idle_movie',
    'pages',
    'seed',
    'patch_cycle_seeds',
    'fill_in_styles',
    'background_color',
    'foreground_brightness_limit',
    'switches',
    'style',
    'pad',
    'contrast',
    'displays',
    'patch_views',
    'tags',
    'tag_styles',
    'parameters',
)
