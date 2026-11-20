"""Portable endpoint records shared by setup compilation and editable export."""

import struct

from .models import MidiEndpoint, Setup, Target


def pack(target: Target) -> bytes:
    return struct.pack('<4sH', b'CEND', len(target.midi_endpoints)) + b''.join(
        RECORD.pack(
            e.route,
            TRANSPORTS.index(e.transport),
            int(e.input) | int(e.output) << 1,
            255 if e.port is None else e.port,
            e.name.encode('ascii'),
            e.label.encode('ascii'),
        )
        for e in target.midi_endpoints
    )


def unpack(data: bytes, offset: int) -> list[MidiEndpoint]:
    if data[offset : offset + 4] != b'CEND' or offset + 6 > len(data):
        raise ValueError('missing MIDI endpoint metadata')
    count = struct.unpack_from('<H', data, offset + 4)[0]
    if count > 16 or offset + 6 + count * RECORD.size > len(data):
        raise ValueError('invalid MIDI endpoint count')
    result: list[MidiEndpoint] = []
    for i in range(count):
        route, transport, flags, port, name, label = RECORD.unpack_from(
            data, offset + 6 + i * RECORD.size
        )
        if transport >= len(TRANSPORTS) or flags > 3 or (port > 3 and port != 255):
            raise ValueError('invalid MIDI endpoint record')
        result.append(
            MidiEndpoint(
                name=_text(name),
                label=_text(label),
                route=route,
                transport=TRANSPORTS[transport],
                input=bool(flags & 1),
                output=bool(flags & 2),
                port=None if port == 255 else port,
            )
        )
    if (
        len({e.name for e in result}) != count
        or len({e.route for e in result}) != count
    ):
        raise ValueError('duplicate MIDI endpoint')
    if sorted(e.route for e in result) != list(range(count)):
        raise ValueError('MIDI routes are not a dense table')
    return result


def ports(endpoints: list[MidiEndpoint]) -> list[str]:
    result = [''] * 4
    for e in endpoints:
        if e.port is not None:
            if result[e.port]:
                raise ValueError('duplicate configurable port slot')
            result[e.port] = e.name
    return result


def enabled(endpoint: MidiEndpoint, setup: Setup) -> bool:
    return (
        endpoint.output
        and (endpoint.transport != 'usb-device' or setup.usb_device_midi)
        and (endpoint.transport != 'usb-host' or setup.usb_host_midi)
    )


def _text(data: bytes) -> str:
    text, separator, padding = data.partition(b'\0')
    if not separator or not text or any(padding):
        raise ValueError('invalid MIDI endpoint text')
    return text.decode('ascii')


TRANSPORTS = ('physical', 'usb-device', 'usb-host')
RECORD = struct.Struct('<BBBB32s32s')
