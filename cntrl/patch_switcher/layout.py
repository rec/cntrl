"""Compile independently cycling regions and their display allocation."""

from . import models


def pack(show: models.Setup, windows: list[str]) -> bytes:
    pages = show.pages
    regions = show.regions
    if not regions and pages:
        if len(windows) == 1:
            regions = {'main': models.PageRegion(pages=list(pages))}
        else:
            performance = [n for n, p in pages.items() if p.type == 'patch-switcher']
            tools = [n for n, p in pages.items() if p.type != 'patch-switcher']
            regions = {
                n: models.PageRegion(pages=p)
                for n, p in [('performance', performance), ('tools', tools)]
                if p
            }
    settings = show.layout or models.PageLayout()
    order = settings.order or list(regions)
    if len(order) != len(regions) or set(order) != set(regions):
        raise ValueError('layout.order must contain each region exactly once')
    if settings.focus is not None and settings.focus not in regions:
        raise ValueError('layout.focus must name a configured region')
    assignments = [p for n in order for p in regions[n].pages]
    if len(assignments) != len(pages) or set(assignments) != set(pages):
        raise ValueError('regions must assign every page exactly once')
    records = bytearray()
    for name, page in pages.items():
        if page.min_windows > page.maximum_windows(len(windows)):
            raise ValueError(
                f'pages.{name}.min_windows exceeds the page type maximum '
                'or target display count'
            )
        priority = page.give_up_windows or list(reversed(windows))
        if len(priority) != len(windows) or set(priority) != set(windows):
            raise ValueError(
                f'pages.{name}.give_up_windows must list every target display ID once'
            )
        records.extend(encoded_name(name))
        records.extend((PAGE_TYPES.index(page.type), page.min_windows))
        records.extend(windows.index(n) for n in priority)
    largest = [max(regions[n].pages, key=lambda p: pages[p].min_windows) for n in order]
    if sum(pages[p].min_windows for p in largest) > len(windows):
        claims = ', '.join(
            f'{n}/{p}={pages[p].min_windows}'
            for n, p in zip(order, largest, strict=True)
        )
        raise ValueError(
            f'region minimum windows exceed the target display count: {claims}'
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
            (len(pages), len(windows), 2, len(order), settings.left_wins, focus, 0, 0)
        )
        + b''.join(encoded_name(n) for n in windows)
        + records
        + region_records
        + cycles
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
        count, windows, _, reserved = data[offset + 4 : offset + 8]
        end = offset + 8 + windows * 32 + count * (34 + windows)
        if reserved or not windows or end > len(data):
            raise ValueError('invalid legacy page layout')
        return end
    if offset + 12 > len(data) or data[offset : offset + 4] != b'CPAG':
        raise ValueError('missing or truncated page layout')
    count, windows, version, regions, left_wins, focus, first, second = data[
        offset + 4 : offset + 12
    ]
    end = offset + 12 + windows * 32 + count * (35 + windows) + regions * 34
    if (
        version != 2
        or first
        or second
        or not windows
        or regions > count
        or bool(count) != bool(regions)
        or left_wins > 1
        or (regions and focus >= regions)
        or (not regions and focus)
        or end > len(data)
    ):
        raise ValueError('invalid page layout')
    return end


def upgrade(data: bytes, offset: int) -> bytes:
    """Keep the legacy single-switcher view beside its independently cycling tools."""
    end = section_end(data, offset)
    if data[offset + 6] == 2:
        return data[offset:end]
    count, windows = data[offset + 4 : offset + 6]
    converted = (
        b'CPAG'
        + bytes((count, windows, 2, bool(count), 0, 0, 0, 0))
        + data[offset + 8 : end]
        + (
            encoded_name('main') + bytes((0, count)) + bytes(range(count))
            if count
            else b''
        )
    )
    settings = models.Setup.model_validate(unpack(converted, 0))
    switchers = [n for n, p in settings.pages.items() if p.type == 'patch-switcher']
    if len(switchers) == 1 and windows >= 2:
        if count == 255:
            raise ValueError(
                'legacy page layout has no room for the full-performance page'
            )
        switcher = switchers[0]
        pages = dict(settings.pages)
        pages[switcher] = pages[switcher].model_copy(
            update={'min_windows': min(pages[switcher].min_windows, 1)}
        )
        placeholder, suffix = 'full-performance', 2
        while placeholder in pages:
            placeholder = f'full-performance-{suffix}'
            suffix += 1
        pages[placeholder] = models.Page(
            type='None', give_up_windows=pages[switcher].give_up_windows
        )
        tools = [placeholder if n == switcher else n for n in settings.pages]
        settings = settings.model_copy(
            update={
                'pages': pages,
                'regions': {
                    'performance': models.PageRegion(pages=[switcher]),
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
            for i in range(windows)
        ]
        return pack(settings, names)
    return converted


def estimate_ram(
    pages: dict[str, models.Page],
    instruments: int,
    cycles: int,
    *,
    switches: bool,
    regions: int,
) -> int:
    # Cortex-M7 ABI. Include checked storage and allocator overhead for each array.
    def charge(size: int) -> int:
        return (size + 7) // 8 * 8 + 8 if size else 0

    controllers = max(1, sum(p.type == 'patch-switcher' for p in pages.values()))
    labels = len(pages) + regions
    return (
        controllers * charge(instruments * 8 + cycles * 4)
        + charge((controllers - 1) * 364)
        + charge((labels + (3 if switches else 2)) * 4 if pages else 0)
        + charge(labels * 96)
        + charge(len(pages) * 272)
        + charge(regions * 12)
    )


def unpack(data: bytes, offset: int) -> dict[str, object]:
    end = section_end(data, offset)
    if data[offset + 6] == 1:
        return unpack(upgrade(data, offset), 0)
    count, window_count = data[offset + 4 : offset + 6]
    region_count, left_wins, focus = data[offset + 7 : offset + 10]
    cursor = offset + 12
    windows = [
        data[cursor + i * 32 : cursor + (i + 1) * 32].split(b'\0')[0].decode('ascii')
        for i in range(window_count)
    ]
    cursor += window_count * 32
    canonical = bytearray(data[offset:end])
    pages = {}
    for _ in range(count):
        name = data[cursor : cursor + 32].split(b'\0')[0].decode('ascii')
        if name in pages or data[cursor + 32] >= len(PAGE_TYPES):
            raise ValueError('invalid page record')
        if data[cursor + 33] > window_count:
            raise ValueError('invalid page window count')
        priority = data[cursor + 34 : cursor + 34 + window_count]
        if sorted(priority) != list(range(window_count)):
            raise ValueError('invalid logical window priority')
        pages[name] = {
            'type': PAGE_TYPES[data[cursor + 32]],
            'min_windows': data[cursor + 33],
            'give_up_windows': [windows[i] for i in priority],
        }
        if pages[name]['type'] not in ('None', 'patch-switcher'):
            # Stored minima remain readable, but tool pages now have fixed width.
            pages[name]['min_windows'] = 1
            canonical[cursor - offset + 33] = 1
        cursor += 34 + window_count
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
            raise ValueError('invalid region cycle')
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
    if pack(models.Setup.model_validate(result), windows) != canonical:
        raise ValueError('invalid page layout records')
    return result


PAGE_TYPES = (
    'tap-tempo',
    'status',
    'firmware',
    'cable-test',
    'panic',
    'trs-ports',
    'expression',
    'clock-transport',
    'clock-tempo',
    'keyboard',
    'None',
    'patch-switcher',
    'midi-monitor',
    'midi-looper',
    'arpeggiator',
)
