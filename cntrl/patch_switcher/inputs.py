"""Declared input identifiers shared by setup compilation and editable export."""

import struct

from .models import Target


def pack(target: Target) -> bytes:
    return HEADER.pack(b'CINP', len(target.buttons)) + b''.join(
        RECORD.pack(b.encode('ascii')) for b in target.buttons
    )


def button_mask(pair: str | None, target: Target) -> int:
    buttons = (
        pair.split('+')
        if pair is not None
        else [target.controls.next, target.controls.confirm]
    )
    if (
        any(b is None or b not in target.buttons for b in buttons)
        or buttons[0] == buttons[1]
    ):
        raise ValueError(
            'stock firmware exit buttons unavailable; specify two declared inputs'
        )
    return sum(1 << target.buttons.index(b) for b in buttons if b is not None)


def unpack(data: bytes, offset: int) -> list[str]:
    if offset + HEADER.size > len(data) or data[offset : offset + 4] != b'CINP':
        raise ValueError('missing input metadata')
    _, count = HEADER.unpack_from(data, offset)
    if count > 8 or offset + HEADER.size + count * RECORD.size > len(data):
        raise ValueError('invalid input metadata size')
    result = []
    for i in range(count):
        raw = RECORD.unpack_from(data, offset + HEADER.size + i * RECORD.size)[0]
        text, separator, padding = raw.partition(b'\0')
        if not separator or not text or any(padding):
            raise ValueError('invalid input identifier')
        label = text.decode('ascii')
        if (
            label in result
            or '+' in label
            or any(not 32 <= ord(c) <= 126 for c in label)
        ):
            raise ValueError('invalid or duplicate input identifier')
        result.append(label)
    return result


HEADER = struct.Struct('<4sH')
RECORD = struct.Struct('<32s')
