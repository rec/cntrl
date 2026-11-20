"""Qualified relay capabilities and per-setup resting contact states."""

import struct

from .models import Setup, Target


def pack(show: Setup, target: Target) -> bytes:
    unknown = show.relays.keys() - target.relay_outputs
    if unknown:
        raise ValueError(f'relays: unsupported contacts {sorted(unknown)}')
    if not target.relay_outputs:
        return b''
    return HEADER.pack(b'CREL', len(target.relay_outputs)) + b''.join(
        RECORD.pack(n.encode('ascii'), show.relays.get(n, False))
        for n in target.relay_outputs
    )


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CREL':
        return offset
    if offset + HEADER.size > len(data):
        raise ValueError('truncated relay metadata')
    count = HEADER.unpack_from(data, offset)[1]
    end = offset + HEADER.size + count * RECORD.size
    if not count or count > 32 or end > len(data):
        raise ValueError('invalid relay metadata size')
    return end


def unpack(data: bytes, offset: int) -> dict[str, bool]:
    end = section_end(data, offset)
    result: dict[str, bool] = {}
    for cursor in range(offset + HEADER.size, end, RECORD.size):
        name, resting = RECORD.unpack_from(data, cursor)
        text, separator, padding = name.partition(b'\0')
        if not text or not separator or any(padding) or resting > 1:
            raise ValueError('invalid relay contact record')
        label = text.decode('ascii')
        if label in result:
            raise ValueError('duplicate relay contact')
        result[label] = bool(resting)
    return result


HEADER = struct.Struct('<4sH')
RECORD = struct.Struct('<32sB')
ACTIONS = ('engage', 'disengage', 'toggle', 'pulse')
