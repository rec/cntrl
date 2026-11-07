"""Compile the app instances and the ordered page cycle."""

from . import models


def pack(show: models.Setup, target: models.Target) -> bytes:
    windows = [d.id for d in target.displays]
    window_count = len(windows)
    identities = b''.join(n.encode('ascii').ljust(32, b'\0') for n in windows)
    apps = bytearray()
    for name, app in show.apps.items():
        encoded = name.encode('ascii')
        if not encoded or len(encoded) > 31 or any(c < 32 or c > 126 for c in encoded):
            raise ValueError(
                'app names must contain 1 to 31 printable ASCII characters'
            )
        apps.extend(encoded.ljust(32, b'\0'))
        priority = app.give_up_windows or list(reversed(windows))
        if len(priority) != window_count or set(priority) != set(windows):
            raise ValueError(
                f'apps.{name}.give_up_windows must contain every target display ID once'
            )
        apps.extend(windows.index(n) for n in priority)
    if any(p.windows > window_count for p in show.pages):
        raise ValueError('page requests more windows than the target provides')
    pages = bytes(v for p in show.pages for v in (PAGE_TYPES.index(p.type), p.windows))
    return (
        b'CLAY'
        + bytes((len(show.apps), len(show.pages), window_count, 2))
        + identities
        + apps
        + pages
    )


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CLAY':
        raise ValueError('missing app/page layout')
    if offset + 8 > len(data):
        raise ValueError('truncated app/page layout')
    apps, pages, windows, version = data[offset + 4 : offset + 8]
    end = offset + 8 + windows * 32 + apps * (32 + windows) + pages * 2
    if version != 2 or not windows or apps > 4 or pages > 32 or end > len(data):
        raise ValueError('invalid app/page layout')
    return end


def unpack(data: bytes, offset: int) -> dict[str, object]:
    section_end(data, offset)
    apps, pages = data[offset + 4 : offset + 6]
    window_count = data[offset + 6]
    cursor = offset + 8
    windows = [
        data[cursor + i * 32 : cursor + (i + 1) * 32].split(b'\0')[0].decode('ascii')
        for i in range(window_count)
    ]
    cursor += window_count * 32
    instances: dict[str, object] = {}
    for _ in range(apps):
        name = data[cursor : cursor + 32].split(b'\0')[0].decode('ascii')
        instances[name] = {
            'type': 'patch-switcher',
            'give_up_windows': [
                windows[i] for i in data[cursor + 32 : cursor + 32 + window_count]
            ],
        }
        cursor += 32 + window_count
    cycle = [
        {'type': PAGE_TYPES[data[cursor + 2 * i]], 'windows': data[cursor + 2 * i + 1]}
        for i in range(pages)
    ]
    return {'apps': instances, 'pages': cycle}


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
    names = bytearray()
    for name in window_ids:
        encoded = name.encode('ascii')
        if not 1 <= len(encoded) <= 31 or any(c < 32 or c > 126 for c in encoded):
            raise ValueError('invalid legacy window ID')
        names.extend(encoded.ljust(32, b'\0'))
    for i in range(apps):
        start = offset + 6 + i * 35
        if set(data[start + 32 : start + 35]) != {0, 1, 2}:
            raise ValueError('invalid legacy window priority')
    section = b'CLAY' + bytes((apps, pages, 3, 2)) + names + data[offset + 6 : end]
    return bytes(section), end


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
)
