"""Banked patches own their banks; pages are views of their running instances."""

import struct

from . import inputs, models


def pack(show: models.Setup, named: dict[str, int], nodes: list[bytes]) -> bytes:
    records = bytearray()
    count = 0
    for index, page in enumerate(show.pages.values()):
        if page.type != 'banked':
            continue
        if page.patch not in named or nodes[named[page.patch]][0] != 17:
            raise ValueError('Banked Page must reference a named banked patch')
        node = named[page.patch]
        if any(
            struct.unpack_from('<H', records, i + 2)[0] == node
            for i in range(0, len(records), 4)
        ):
            raise ValueError('each banked patch must have exactly one Banked Page')
        records.extend(struct.pack('<HH', index, node))
        count += 1
    if {n for n, v in enumerate(nodes) if v[0] == 17} != {
        struct.unpack_from('<H', records, i + 2)[0] for i in range(0, len(records), 4)
    }:
        raise ValueError('each banked patch must have exactly one Banked Page')
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


def definition(
    value: models.Banked,
    nodes: dict[str, int],
    show: models.Setup,
    target: models.Target,
) -> bytes:
    if value.initial_bank >= len(value.banks):
        raise ValueError('banked patch initial_bank is outside its bank collection')
    result = bytearray(
        struct.pack(
            '<HHHH', len(nodes), len(value.banks), value.initial_bank, len(value.chords)
        )
    )
    for bank in value.banks:
        if not 1 <= len(bank) <= 4:
            raise ValueError('banks must have one to four preset pages')
        result.extend(struct.pack('<H', len(bank)))
        for name in bank:
            if name not in nodes:
                raise ValueError(f'unknown bank preset page {name!r}')
            result.extend(struct.pack('<H', nodes[name]))
    for operation, buttons in value.chords.items():
        if operation not in OPERATIONS or len(buttons) < 2:
            raise ValueError(
                'banked chords need a bank/page/menu operation and two buttons'
            )
        mask = sum(1 << inputs.index(b, show, target) for b in buttons)
        if mask >= 1 << 32 or mask.bit_count() != len(buttons):
            raise ValueError('banked chord inputs must be distinct and below index 32')
        result.extend(struct.pack('<BI', OPERATIONS[operation], mask))
    for name, node in nodes.items():
        encoded = name.encode('ascii')
        if not encoded or len(encoded) >= 64:
            raise ValueError(
                'bank preset-page names must have 1 to 63 ASCII characters'
            )
        result.extend(struct.pack('<H64s', node, encoded))
    return bytes(result)


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
    data: bytes, offset: int, names: dict[int, str]
) -> dict[int, dict[str, object]]:
    end = section_end(data, offset)
    result: dict[int, dict[str, object]] = {}
    if end == offset:
        return result
    count = struct.unpack_from('<H', data, offset + 8)[0]
    if end != offset + 10 + count * 4:
        raise ValueError('invalid banked page records')
    for i in range(count):
        page, node = struct.unpack_from('<HH', data, offset + 10 + i * 4)
        result[page] = {'patch': names[node]}
    return result


OPERATIONS = {
    'next-bank': 14,
    'previous-bank': 15,
    'next-bank-page': 16,
    'previous-bank-page': 17,
    'menu': 9,
}
