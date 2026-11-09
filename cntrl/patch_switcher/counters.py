"""Named MIDI counter configuration in compiled setup documents."""

import struct

from . import midi_endpoints, models


def pack(
    counters: dict[str, models.Counter], show: models.Setup, target: models.Target
) -> bytes:
    if not counters:
        return b''
    if len(counters) > 65535:
        raise ValueError('too many counters')
    outputs = {
        e.name
        for e in target.midi_endpoints
        if e.output and midi_endpoints.enabled(e, show)
    }
    records: list[bytes] = []
    for name, counter in counters.items():
        if (
            not name
            or len(name.encode()) > 63
            or any(ord(c) < 32 or ord(c) == 127 for c in name)
        ):
            raise ValueError(
                'counter names require 1..63 UTF-8 bytes without control characters'
            )
        if counter.output not in outputs:
            raise ValueError(
                f'counters.{name}: unavailable MIDI output {counter.output!r}'
            )
        offset = int(counter.type == 'program-change')
        records.append(
            RECORD.pack(
                name.encode(),
                counter.output.encode(),
                (0xC0 if offset else 0xB0) | (counter.channel - 1),
                counter.controller or 0,
                counter.initial - offset,
                counter.minimum - offset,
                counter.maximum - offset,
                counter.step,
                counter.boundary == 'bounce',
            )
        )
    return HEADER.pack(b'CCNT', len(records)) + b''.join(records)


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CCNT':
        return offset
    if offset + HEADER.size > len(data):
        raise ValueError('truncated counter metadata')
    count = HEADER.unpack_from(data, offset)[1]
    end = offset + HEADER.size + count * RECORD.size
    if not count or end > len(data):
        raise ValueError('invalid counter metadata size')
    return end


def unpack(data: bytes, offset: int) -> dict[str, models.Counter]:
    end = section_end(data, offset)
    result: dict[str, models.Counter] = {}
    for cursor in range(offset + HEADER.size, end, RECORD.size):
        name, output, status, controller, initial, minimum, maximum, step, bounce = (
            RECORD.unpack_from(data, cursor)
        )
        bias = int(status >> 4 == 12)
        label = name.split(b'\0')[0].decode()
        result[label] = models.Counter(
            type='program-change' if bias else 'control-change',
            output=output.split(b'\0')[0].decode(),
            channel=(status & 15) + 1,
            controller=None if bias else controller,
            initial=initial + bias,
            minimum=minimum + bias,
            maximum=maximum + bias,
            step=step,
            boundary='bounce' if bounce else 'wrap',
        )
    return result


HEADER = struct.Struct('<4sH')
RECORD = struct.Struct('<64s32sBBBBBh?')
