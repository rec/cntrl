"""Declared input identifiers shared by setup compilation and editable export."""

import struct

from . import midi_endpoints
from .models import Setup, Target


def pack(target: Target) -> bytes:
    names = labels(target)
    return HEADER.pack(b'CINP', len(names)) + b''.join(
        RECORD.pack(b.encode('ascii')) for b in names
    )


def labels(target: Target) -> list[str]:
    names = target.buttons + [
        f'{p}.{c}'
        for p in midi_endpoints.ports(target.midi_endpoints)
        if 'aux' in target.trs_ports.get(p, [])
        for c in ('tip', 'ring', 'both')
    ]
    if (
        len(names) > 32
        or any(len(n.encode('ascii')) > 31 for n in names)
        or len(names) != len(set(names))
    ):
        raise ValueError(
            'input identifiers must be unique, fit 31 bytes and total at most 32'
        )
    return names


def index(name: str, show: Setup, target: Target) -> int:
    names = labels(target)
    if name not in names:
        raise ValueError(f'unknown switch {name!r}')
    if name not in target.buttons:
        port, contact = name.rsplit('.', 1)
        if show.trs_ports.get(port) != 'aux':
            raise ValueError(f'{name}: requires the aux TRS role')
        if (
            contact == 'both'
            and show.auxiliary_switches.get(port, 'independent') != 'combined'
        ):
            raise ValueError(f'{name}: requires combined auxiliary switches')
    return names.index(name)


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
    if count > 32 or offset + HEADER.size + count * RECORD.size > len(data):
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
