"""Setup-wide loop-stop policy and explicit continuous neutral values."""

import struct

from . import midi_endpoints, models


def pack(show: models.Setup, target: models.Target) -> bytes:
    if not show.loop_stop_reset_controls and not show.loop_stop_neutral_values:
        return b''
    outputs = {
        e.name: e.route
        for e in target.midi_endpoints
        if e.output
        and midi_endpoints.enabled(e, show)
        and (e.port is None or show.trs_ports.get(e.name) in ('midi-a', 'midi-b'))
    }
    records: list[bytes] = []
    for v in show.loop_stop_neutral_values:
        if v.output not in outputs:
            raise ValueError(
                f'loop_stop_neutral_values: unavailable output {v.output!r}'
            )
        assert v.channel is not None
        if isinstance(v, models.ControlChange):
            kind, address = (1 if v.resolution == 7 else 2), v.controller
        elif isinstance(v, models.Nrpn):
            kind, address = (6 if v.resolution == 7 else 7), v.parameter
        else:
            kind, address = 8, v.parameter
        records.append(
            RECORD.pack(outputs[v.output], kind, v.channel - 1, address, v.value)
        )
    return HEADER.pack(b'CLRS', show.loop_stop_reset_controls, len(records)) + b''.join(
        records
    )


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CLRS':
        return offset
    if len(data) - offset < HEADER.size:
        raise ValueError('truncated loop reset settings')
    _, enabled, count = HEADER.unpack_from(data, offset)
    end = offset + HEADER.size + count * RECORD.size
    if enabled > 1 or end > len(data):
        raise ValueError('invalid loop reset settings')
    values: list[models.ControlChange | models.Nrpn | models.Rpn] = []
    for i in range(count):
        output, kind, channel, address, value = RECORD.unpack_from(
            data, offset + HEADER.size + i * RECORD.size
        )
        if output > 15 or channel > 15 or kind not in (1, 2, 6, 7, 8):
            raise ValueError('invalid loop reset parameter')
        if kind in (1, 2):
            values.append(
                models.ControlChange(
                    output=str(output),
                    channel=channel + 1,
                    controller=address,
                    value=value,
                    type='control-change',
                    resolution=7 if kind == 1 else 14,
                )
            )
        elif kind in (6, 7):
            values.append(
                models.Nrpn(
                    output=str(output),
                    channel=channel + 1,
                    parameter=address,
                    value=value,
                    type='nrpn',
                    resolution=7 if kind == 6 else 14,
                )
            )
        else:
            values.append(
                models.Rpn(
                    output=str(output),
                    channel=channel + 1,
                    parameter=address,
                    value=value,
                    type='rpn',
                )
            )
    models.Setup.neutral_values(values)
    return end


def unpack(data: bytes, offset: int, names: dict[int, str]) -> dict[str, object]:
    if section_end(data, offset) == offset:
        return {}
    _, enabled, count = HEADER.unpack_from(data, offset)
    values: list[dict[str, object]] = []
    for i in range(count):
        output, kind, channel, address, value = RECORD.unpack_from(
            data, offset + HEADER.size + i * RECORD.size
        )
        entry: dict[str, object] = dict(
            output=names[output], channel=channel + 1, value=value
        )
        if kind in (1, 2):
            entry.update(
                type='control-change',
                controller=address,
                resolution=7 if kind == 1 else 14,
            )
        else:
            entry.update(type='rpn' if kind == 8 else 'nrpn', parameter=address)
            if kind != 8:
                entry['resolution'] = 7 if kind == 6 else 14
        values.append(entry)
    result: dict[str, object] = {'loop_stop_reset_controls': bool(enabled)}
    if values:
        result['loop_stop_neutral_values'] = values
    return result


HEADER = struct.Struct('<4sBH')
RECORD = struct.Struct('<BBBHH')
