"""Compile named pages and their window allocation."""

from . import models


def pack(pages: dict[str, models.Page], windows: list[str]) -> bytes:
    records = bytearray()
    if sum(p.type == 'patch-switcher' for p in pages.values()) > 4:
        raise ValueError('at most four patch-switcher pages are supported')
    for name, page in pages.items():
        encoded = name.encode('ascii')
        if not encoded or len(encoded) > 31 or any(c < 32 or c > 126 for c in encoded):
            raise ValueError(
                'page names must contain 1 to 31 printable ASCII characters'
            )
        count = page.windows
        if count is None:
            count = (
                len(windows)
                if page.type == 'patch-switcher'
                else 0
                if page.type == 'None'
                else 1
            )
        if count > len(windows):
            raise ValueError('page requests more windows than the target provides')
        priority = page.give_up_windows or list(reversed(windows))
        if len(priority) != len(windows) or set(priority) != set(windows):
            raise ValueError(
                f'pages.{name}.give_up_windows must list every target display ID once'
            )
        records.extend(encoded.ljust(32, b'\0'))
        records.extend((PAGE_TYPES.index(page.type), count))
        records.extend(windows.index(n) for n in priority)
    return (
        b'CPAG'
        + bytes((len(pages), len(windows), 1, 0))
        + b''.join(n.encode('ascii').ljust(32, b'\0') for n in windows)
        + records
    )


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CPAG':
        raise ValueError('missing page layout')
    if offset + 8 > len(data):
        raise ValueError('truncated page layout')
    count, windows, version, reserved = data[offset + 4 : offset + 8]
    end = offset + 8 + windows * 32 + count * (34 + windows)
    if version != 1 or reserved or not windows or count > 36 or end > len(data):
        raise ValueError('invalid page layout')
    return end


def unpack(data: bytes, offset: int) -> dict[str, object]:
    section_end(data, offset)
    count, window_count = data[offset + 4 : offset + 6]
    cursor = offset + 8
    windows = [
        data[cursor + i * 32 : cursor + (i + 1) * 32].split(b'\0')[0].decode('ascii')
        for i in range(window_count)
    ]
    cursor += window_count * 32
    pages = {}
    for _ in range(count):
        name = data[cursor : cursor + 32].split(b'\0')[0].decode('ascii')
        pages[name] = {
            'type': PAGE_TYPES[data[cursor + 32]],
            'windows': data[cursor + 33],
            'give_up_windows': [
                windows[i] for i in data[cursor + 34 : cursor + 34 + window_count]
            ],
        }
        cursor += 34 + window_count
    return {'pages': pages}


def upgrade_legacy_section(
    data: bytes, offset: int, window_ids: list[str]
) -> tuple[bytes, int]:
    """Convert the format-34 three-window record without touching its graph.

    The caller supplies the historical window identities and replaces the
    returned section through the old end offset before updating the setup CRC.
    """
    if len(window_ids) != 3 or len(set(window_ids)) != 3:
        raise ValueError('legacy layout needs its three distinct window IDs')
    if offset + 6 > len(data) or data[offset : offset + 4] != b'CLAY':
        raise ValueError('missing legacy app/page layout')
    apps, pages = data[offset + 4 : offset + 6]
    end = offset + 6 + apps * 35 + pages * 2
    if apps > 4 or pages > 32 or end > len(data):
        raise ValueError('invalid legacy app/page layout')
    entries: dict[str, models.Page] = {}
    old_names = {
        data[offset + 6 + i * 35 : offset + 38 + i * 35].split(b'\0')[0].decode('ascii')
        for i in range(apps)
    }
    for i in range(pages):
        kind, count = data[
            offset + 6 + apps * 35 + i * 2 : offset + 8 + apps * 35 + i * 2
        ]
        name = PAGE_TYPES[kind]
        suffix = 1
        while name in entries or name in old_names:
            name = f'{PAGE_TYPES[kind]}-{suffix}'
            suffix += 1
        entries[name] = models.Page(type=PAGE_TYPES[kind], windows=count)
    for i in range(apps):
        start = offset + 6 + i * 35
        name = data[start : start + 32].split(b'\0')[0].decode('ascii')
        priority = [window_ids[j] for j in data[start + 32 : start + 35]]
        entries[name] = models.Page(give_up_windows=priority)
    return pack(entries, window_ids), end


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
)
