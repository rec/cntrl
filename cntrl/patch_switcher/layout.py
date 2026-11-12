"""Compile named pages and their window allocation."""

from . import models


def pack(pages: dict[str, models.Page], windows: list[str]) -> bytes:
    records = bytearray()
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
                else 2
                if page.type == 'midi-monitor'
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
    if version != 1 or reserved or not windows or end > len(data):
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


def estimate_ram(
    pages: dict[str, models.Page], instruments: int, cycles: int, *, switches: bool
) -> int:
    # Cortex-M7 ABI: Controller is 360 bytes, pointers/int are 4, cursors are 2.
    # The primary Controller object is linked into RAM1, but its counted state
    # and every additional Controller object are checked RAM2 allocations.
    def charge(size: int) -> int:
        return (size + 7) // 8 * 8 + 8 if size else 0

    controllers = max(1, sum(p.type == 'patch-switcher' for p in pages.values()))
    names = (len(pages) + (3 if switches else 2)) * 4 if pages else 0
    return (
        controllers * charge(instruments * 8 + cycles * 4)
        + charge((controllers - 1) * 360)
        + charge(names)
    )


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
