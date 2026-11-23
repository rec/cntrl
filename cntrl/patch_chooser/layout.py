"""Compile independently cycling regions and their display allocation."""

from pydantic import ValidationError

from . import models
from .binary_format import DocumentFormatError


class LayoutError(ValueError):
    """The authored page layout cannot be packed with the declared displays."""


def pack(show: models.Setup, displays: list[str], navigation: bytes = b'') -> bytes:
    pages = show.pages
    regions = show.regions
    if not regions and pages:
        if len(displays) == 1:
            regions = {'main': models.PageRegion(pages=list(pages))}
        else:
            performance = [
                n for n, p in pages.items() if p.type in ('patch-chooser', 'banked')
            ]
            tools = [
                n for n, p in pages.items() if p.type not in ('patch-chooser', 'banked')
            ]
            regions = {
                n: models.PageRegion(pages=p)
                for n, p in [('performance', performance), ('tools', tools)]
                if p
            }
    settings = show.layout or models.PageLayout()
    order = settings.order or list(regions)
    if len(order) != len(regions) or set(order) != set(regions):
        raise LayoutError('layout.order must contain each region exactly once')
    if settings.focus is not None and settings.focus not in regions:
        raise LayoutError('layout.focus must name a configured region')
    assignments = [p for n in order for p in regions[n].pages]
    if len(assignments) != len(pages) or set(assignments) != set(pages):
        raise LayoutError('regions must assign every page exactly once')
    records = bytearray()
    for name, page in pages.items():
        if page.min_displays > page.maximum_displays(len(displays)):
            raise LayoutError(
                f'pages.{name}.min_displays exceeds the page type maximum '
                'or target display count'
            )
        priority = page.give_up_displays or list(reversed(displays))
        if len(priority) != len(displays) or set(priority) != set(displays):
            raise LayoutError(
                f'pages.{name}.give_up_displays must list every target display ID once'
            )
        records.extend(encoded_name(name))
        records.extend((PAGE_TYPES.index(page.type), page.min_displays))
        records.extend(displays.index(n) for n in priority)
    largest = [
        max(regions[n].pages, key=lambda p: pages[p].min_displays) for n in order
    ]
    if sum(pages[p].min_displays for p in largest) > len(displays):
        claims = ', '.join(
            f'{n}/{p}={pages[p].min_displays}'
            for n, p in zip(order, largest, strict=True)
        )
        raise LayoutError(
            f'region minimum displays exceed the target display count: {claims}'
        )
    region_records = bytearray()
    cycles = bytearray()
    names = list(pages)
    for name in order:
        region_records.extend(encoded_name(name))
        region_records.extend((len(cycles), len(regions[name].pages)))
        cycles.extend(names.index(p) for p in regions[name].pages)
    focus = order.index(settings.focus) if settings.focus is not None else 0
    return (
        b'CPAG'
        + bytes(
            (
                len(pages),
                len(displays),
                2,
                len(order),
                settings.left_wins,
                focus,
                len(navigation),
                0,
            )
        )
        + b''.join(encoded_name(n) for n in displays)
        + records
        + region_records
        + cycles
        + navigation
    )


def encoded_name(name: str) -> bytes:
    encoded = name.encode('ascii')
    if not encoded or len(encoded) > 31 or any(c < 32 or c > 126 for c in encoded):
        raise ValueError(
            'page, region and display names must contain '
            '1 to 31 printable ASCII characters'
        )
    return encoded.ljust(32, b'\0')


def section_end(data: bytes, offset: int) -> int:
    if (
        offset + 8 <= len(data)
        and data[offset : offset + 4] == b'CPAG'
        and data[offset + 6] == 1
    ):
        count, displays, _, reserved = data[offset + 4 : offset + 8]
        end = offset + 8 + displays * 32 + count * (34 + displays)
        if reserved or not displays or end > len(data):
            raise DocumentFormatError('invalid legacy page layout')
        return end
    if offset + 12 > len(data) or data[offset : offset + 4] != b'CPAG':
        raise DocumentFormatError('missing or truncated page layout')
    count, displays, version, regions, left_wins, focus, first, second = data[
        offset + 4 : offset + 12
    ]
    end = offset + 12 + displays * 32 + count * (35 + displays) + regions * 34 + first
    if (
        version != 2
        or first not in (0, 4)
        or second
        or not displays
        or regions > count
        or bool(count) != bool(regions)
        or left_wins > 1
        or (regions and focus >= regions)
        or (not regions and focus)
        or end > len(data)
    ):
        raise DocumentFormatError('invalid page layout')
    return end


def upgrade(data: bytes, offset: int, *, migrate_retired_pages: bool = False) -> bytes:
    """Upgrade layout records and migrate retired tools to the current page set."""
    end = section_end(data, offset)
    if data[offset + 6] == 2:
        section = data[offset:end]
        return _replace_retired_pages(section) if migrate_retired_pages else section
    count, displays = data[offset + 4 : offset + 6]
    converted = (
        b'CPAG'
        + bytes((count, displays, 2, bool(count), 0, 0, 0, 0))
        + data[offset + 8 : end]
        + (
            encoded_name('main') + bytes((0, count)) + bytes(range(count))
            if count
            else b''
        )
    )
    if migrate_retired_pages:
        converted = _replace_retired_pages(converted)
    settings = models.Setup.model_validate(unpack(converted, 0))
    choosers = [n for n, p in settings.pages.items() if p.type == 'patch-chooser']
    if len(choosers) == 1 and displays >= 2:
        if len(settings.pages) == 255:
            raise DocumentFormatError(
                'legacy page layout has no room for the full-performance page'
            )
        chooser = choosers[0]
        pages = dict(settings.pages)
        pages[chooser] = pages[chooser].model_copy(
            update={'min_displays': min(pages[chooser].min_displays, 1)}
        )
        placeholder, suffix = 'full-performance', 2
        while placeholder in pages:
            placeholder = f'full-performance-{suffix}'
            suffix += 1
        pages[placeholder] = models.Page(
            type='None', give_up_displays=pages[chooser].give_up_displays
        )
        tools = [placeholder if n == chooser else n for n in settings.pages]
        settings = settings.model_copy(
            update={
                'pages': pages,
                'regions': {
                    'performance': models.PageRegion(pages=[chooser]),
                    'tools': models.PageRegion(pages=tools),
                },
                'layout': models.PageLayout(
                    order=['performance', 'tools'], focus='tools'
                ),
            }
        )
        names = [
            data[offset + 8 + i * 32 : offset + 40 + i * 32]
            .split(b'\0')[0]
            .decode('ascii')
            for i in range(displays)
        ]
        return pack(settings, names)
    return converted


def _replace_retired_pages(data: bytes) -> bytes:
    count, displays, _, regions, left_wins, focus = data[4:10]
    start = 12 + displays * 32
    stride = 34 + displays
    end = start + count * stride
    records = [data[i : i + stride] for i in range(start, end, stride)]
    if not any(r[32] in RETIRED_PAGES for r in records):
        return data

    keep: dict[int, int] = {}
    pages = bytearray()
    time_available = displays >= 2 and not any(r[32] == 19 for r in records)
    for index, record in enumerate(records):
        kind = record[32]
        if kind in RETIRED_PAGES:
            if not time_available or kind not in RETIRED_CLOCK_PAGES:
                continue
            time_available = False
            record = record[:32] + bytes((19, 2)) + record[34:]
        keep[index] = len(keep)
        pages.extend(record)

    region_records = bytearray()
    cycles = bytearray()
    surviving_regions = []
    old_cycles = end + regions * 34
    assigned: list[int] = []
    for index in range(regions):
        position = end + index * 34
        first, length = data[position + 32 : position + 34]
        references = data[old_cycles + first : old_cycles + first + length]
        assigned.extend(references)
        if first != len(assigned) - length or len(references) != length or not length:
            raise DocumentFormatError('invalid legacy region cycle')
        migrated = bytes(keep[p] for p in references if p in keep)
        if migrated:
            surviving_regions.append(index)
            region_records.extend(data[position : position + 32])
            region_records.extend((len(cycles), len(migrated)))
            cycles.extend(migrated)
    if sorted(assigned) != list(range(count)):
        raise DocumentFormatError('legacy layout must assign each page to one region')
    focus = surviving_regions.index(focus) if focus in surviving_regions else 0
    result = (
        b'CPAG'
        + bytes(
            (len(keep), displays, 2, len(surviving_regions), left_wins, focus, 0, 0)
        )
        + data[12:start]
        + pages
        + region_records
        + cycles
    )
    unpack(result, 0)
    return result


def estimate_ram(
    pages: dict[str, models.Page],
    instruments: int,
    cycles: int,
    *,
    regions: int,
) -> int:
    # Cortex-M7 ABI. Include checked storage and allocator overhead for each array.
    def charge(size: int) -> int:
        return (size + 7) // 8 * 8 + 8 if size else 0

    controllers = max(1, sum(p.type == 'patch-chooser' for p in pages.values()))
    return (
        controllers * charge(instruments * 8 + cycles * 4)
        + charge((controllers - 1) * 456)
        + charge(len(pages) * 320)
        + charge(regions * 12)
    )


def unpack(data: bytes, offset: int) -> dict[str, object]:
    end = section_end(data, offset)
    if data[offset + 6] == 1:
        return unpack(upgrade(data, offset), 0)
    count, display_count = data[offset + 4 : offset + 6]
    region_count, left_wins, focus = data[offset + 7 : offset + 10]
    cursor = offset + 12
    displays = [
        data[cursor + i * 32 : cursor + (i + 1) * 32].split(b'\0')[0].decode('ascii')
        for i in range(display_count)
    ]
    cursor += display_count * 32
    canonical = bytearray(data[offset:end])
    pages = {}
    for _ in range(count):
        name = data[cursor : cursor + 32].split(b'\0')[0].decode('ascii')
        if data[cursor + 32] == 0:
            raise DocumentFormatError(
                'Tap Tempo page has been removed; recompile this setup using Time'
            )
        if data[cursor + 32] in (5, 6):
            raise DocumentFormatError(
                'TRS Ports and Expression pages have been removed; recompile this setup'
            )
        if data[cursor + 32] == 9:
            raise DocumentFormatError(
                'Keyboard page has been removed; recompile this setup'
            )
        if data[cursor + 32] in (2, 7, 8):
            raise DocumentFormatError(
                'Clock and Firmware pages have been removed; recompile this setup'
            )
        if data[cursor + 32] == 18:
            raise DocumentFormatError(
                'Menu page has been removed. Recompile this setup with individual '
                'tool pages'
            )
        if name in pages or data[cursor + 32] >= len(PAGE_TYPES):
            raise DocumentFormatError('invalid page record')
        if data[cursor + 33] > display_count:
            raise DocumentFormatError('invalid page display count')
        priority = data[cursor + 34 : cursor + 34 + display_count]
        if sorted(priority) != list(range(display_count)):
            raise DocumentFormatError('invalid logical display priority')
        pages[name] = {
            'type': PAGE_TYPES[data[cursor + 32]],
            'min_displays': data[cursor + 33],
            'give_up_displays': [displays[i] for i in priority],
        }
        if pages[name]['type'] not in (
            'None',
            'patch-chooser',
            'banked',
            'pong',
            'language',
            'time',
        ):
            # Stored minima remain readable, but tool pages now have fixed width.
            pages[name]['min_displays'] = 1
            canonical[cursor - offset + 33] = 1
        cursor += 34 + display_count
    cycles = cursor + region_count * 34
    names = list(pages)
    regions = {}
    assigned = 0
    for i in range(region_count):
        record = cursor + i * 34
        name = data[record : record + 32].split(b'\0')[0].decode('ascii')
        start, length = data[record + 32 : record + 34]
        references = data[cycles + start : cycles + start + length]
        if (
            name in regions
            or start != assigned
            or not length
            or start + length > count
            or any(p >= count for p in references)
        ):
            raise DocumentFormatError('invalid region cycle')
        assigned += length
        regions[name] = {'pages': [names[p] for p in references]}
    result: dict[str, object] = {'pages': pages}
    if regions:
        order = list(regions)
        result.update(
            regions=regions,
            layout={
                'order': order,
                'left_wins': bool(left_wins),
                'focus': order[focus],
            },
        )
    try:
        encoded = pack(
            models.Setup.model_validate(result),
            displays,
            data[end - 4 : end] if data[offset + 10] else b'',
        )
    except (LayoutError, ValidationError) as error:
        raise DocumentFormatError(f'invalid page layout records: {error}') from error
    if encoded != canonical:
        raise DocumentFormatError('invalid page layout records')
    return result


PAGE_TYPES = (
    '',
    'status',
    '',
    'cable-test',
    'panic',
    '',
    '',
    '',
    '',
    '',  # Unassigned page ID. Existing page IDs remain stable.
    'None',
    'patch-chooser',
    'midi-monitor',
    'midi-looper',
    'arpeggiator',
    'banked',
    'pong',
    'language',
    '',
    'time',
    'calibrate',
    'doom',
    'setups',
    'songs-and-setlists',
    'switches',
)

RETIRED_CLOCK_PAGES = (0, 7, 8)
RETIRED_PAGES = (*RETIRED_CLOCK_PAGES, 2, 5, 6, 9)
