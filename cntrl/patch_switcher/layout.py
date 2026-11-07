"""Compile the app instances and the ordered page cycle."""

from . import models


def pack(show: models.Setup) -> bytes:
    apps = bytearray()
    for name, app in show.apps.items():
        encoded = name.encode('ascii')
        if not encoded or len(encoded) > 31 or any(c < 32 or c > 126 for c in encoded):
            raise ValueError(
                'app names must contain 1 to 31 printable ASCII characters'
            )
        apps.extend(encoded.ljust(32, b'\0'))
        apps.extend(WINDOWS.index(n) for n in app.give_up_windows)
    pages = bytes(v for p in show.pages for v in (PAGE_TYPES.index(p.type), p.windows))
    return b'CLAY' + bytes((len(show.apps), len(show.pages))) + apps + pages


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CLAY':
        raise ValueError('missing app/page layout')
    if offset + 6 > len(data):
        raise ValueError('truncated app/page layout')
    apps, pages = data[offset + 4 : offset + 6]
    end = offset + 6 + apps * 35 + pages * 2
    if apps > 4 or pages > 32 or end > len(data):
        raise ValueError('invalid app/page layout')
    return end


def unpack(data: bytes, offset: int) -> dict[str, object]:
    section_end(data, offset)
    apps, pages = data[offset + 4 : offset + 6]
    cursor = offset + 6
    instances: dict[str, object] = {}
    for _ in range(apps):
        name = data[cursor : cursor + 32].split(b'\0')[0].decode('ascii')
        instances[name] = {
            'type': 'patch-switcher',
            'give_up_windows': [WINDOWS[i] for i in data[cursor + 32 : cursor + 35]],
        }
        cursor += 35
    cycle = [
        {'type': PAGE_TYPES[data[cursor + 2 * i]], 'windows': data[cursor + 2 * i + 1]}
        for i in range(pages)
    ]
    return {'apps': instances, 'pages': cycle}


WINDOWS = ('left', 'center', 'right')
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
