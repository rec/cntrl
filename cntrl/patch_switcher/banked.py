"""Banked page references share the setup's ordinary patch graph and counters."""

import struct

from . import inputs, models


def pack(
    show: models.Setup, named: dict[str, int], nodes: list[bytes], target: models.Target
) -> bytes:
    records = bytearray()
    count = 0
    for index, page in enumerate(show.pages.values()):
        if page.type != 'banked':
            continue
        if not page.banks or page.initial_bank >= len(page.banks):
            raise ValueError('banked page needs banks and a valid initial_bank')
        records.extend(
            struct.pack(
                '<HHHH', index, len(page.banks), page.initial_bank, len(page.chords)
            )
        )
        for bank in page.banks:
            if not 1 <= len(bank) <= 4:
                raise ValueError('banks must have one to four preset pages')
            records.extend(struct.pack('<H', len(bank)))
            for name in bank:
                if name not in named or nodes[named[name]][0] != 10:
                    raise ValueError(
                        f'bank page {name!r} must name a patch with bindings'
                    )
                records.extend(struct.pack('<H', named[name]))
        for operation, buttons in page.chords.items():
            if operation not in OPERATIONS or len(buttons) < 2:
                raise ValueError(
                    'banked chords need a bank/page/menu operation '
                    'and at least two buttons'
                )
            mask = sum(1 << inputs.index(b, show, target) for b in buttons)
            if mask >= 1 << 32:
                raise ValueError('banked chords require inputs below index 32')
            if mask.bit_count() != len(buttons):
                raise ValueError('banked chord buttons must be distinct')
            records.extend(struct.pack('<BI', OPERATIONS[operation], mask))
        count += 1
    return (
        b'CBPG' + struct.pack('<IH', 10 + len(records), count) + records
        if count
        else b''
    )


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CBPG':
        return offset
    size = struct.unpack_from('<I', data, offset + 4)[0]
    if size < 10 or offset + size > len(data):
        raise ValueError('invalid banked page section')
    return offset + size


def estimate_ram(pages: dict[str, models.Page], document_bytes: int) -> int:
    # Stable document copy (including the 28-byte persistence envelope), page
    # object and up to eight switch states. The
    # embedded runtime asserts these Cortex-M7 object size bounds. Variable
    # executor queues use the existing runtime reserve, as ordinary switches do.
    count = sum(p.type == 'banked' for p in pages.values())
    return count * (((document_bytes + 28 + 7) // 8 * 8 + 8) + 1048 + 4104) + (
        (len(pages) * 4 + 7) // 8 * 8 + 8 if count else 0
    )


def unpack(
    data: bytes, offset: int, names: dict[int, str], labels: list[str]
) -> dict[int, dict[str, object]]:
    end = section_end(data, offset)
    result = {}
    if end == offset:
        return result
    count = struct.unpack_from('<H', data, offset + 8)[0]
    cursor = offset + 10
    for _ in range(count):
        page, count, initial, chord_count = struct.unpack_from('<HHHH', data, cursor)
        cursor += 8
        banks = []
        for _ in range(count):
            length = struct.unpack_from('<H', data, cursor)[0]
            cursor += 2
            banks.append(
                [names[n] for n in struct.unpack_from(f'<{length}H', data, cursor)]
            )
            cursor += length * 2
        chords = {}
        for _ in range(chord_count):
            operation, mask = struct.unpack_from('<BI', data, cursor)
            cursor += 5
            chords[next(n for n, v in OPERATIONS.items() if v == operation)] = [
                n for i, n in enumerate(labels) if mask & (1 << i)
            ]
        result[page] = dict(banks=banks, initial_bank=initial, chords=chords)
    if cursor != end:
        raise ValueError('invalid banked page records')
    return result


OPERATIONS = {
    'next-bank': 14,
    'previous-bank': 15,
    'next-bank-page': 16,
    'previous-bank-page': 17,
    'menu': 9,
}
