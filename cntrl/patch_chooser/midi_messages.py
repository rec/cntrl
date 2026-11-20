"""Translate named MIDI 1.0 messages and complete parameter transactions."""

from . import models


def encode(value: models.OutputMessage) -> bytes:
    if isinstance(value, models.ControlChange) and value.resolution == 14:
        assert value.channel is not None
        return bytes(
            (
                0xB0 | (value.channel - 1),
                value.controller,
                value.value >> 7,
                0xB0 | (value.channel - 1),
                value.controller + 32,
                value.value & 127,
            )
        )
    if isinstance(value, (models.Nrpn, models.Rpn)):
        assert value.channel is not None
        registered = isinstance(value, models.Rpn)
        resolution = 14 if registered else value.resolution
        messages = [
            (101 if registered else 99, value.parameter >> 7),
            (100 if registered else 98, value.parameter & 127),
            (6, value.value if resolution == 7 else value.value >> 7),
        ]
        if resolution == 14:
            messages.append((38, value.value & 127))
        messages.extend([(101, 127), (100, 127)])
        return b''.join(bytes((0xB0 | (value.channel - 1), c, v)) for c, v in messages)
    if isinstance(value, models.SysEx):
        return bytes((0xF0, *value.data, 0xF7))
    status, fields = MESSAGES[value.type]
    if isinstance(value, models.ChannelMessage):
        if value.channel is None:
            raise ValueError('MIDI channel must be resolved before encoding')
        status |= value.channel - 1
    if isinstance(value, models.ChannelProgramChange):
        return bytes((status, value.program - 1))
    if isinstance(value, models.TimeCodeQuarterFrame):
        return bytes((status, value.piece << 4 | value.value))
    if isinstance(value, models.SongPositionPointer):
        return bytes((status, value.position & 127, value.position >> 7))
    if isinstance(value, models.PitchBend):
        bend = value.value + 8192
        return bytes((status, bend & 127, bend >> 7))
    return bytes((status, *(getattr(value, f) for f in fields)))


def decode(message: bytes) -> dict[str, object]:
    status, *data = message
    if status == 0xF0:
        if not data or data[-1] != 0xF7 or any(b > 127 for b in data[:-1]):
            raise ValueError('invalid SysEx payload')
        return {'type': 'sysex', 'data': data[:-1]}
    code = status & 0xF0 if status < 0xF0 else status
    if code not in TYPES or any(b > 127 for b in data):
        raise ValueError('invalid MIDI message')
    name = TYPES[code]
    result: dict[str, object] = {'type': name}
    if code < 0xF0:
        result['channel'] = (status & 15) + 1
    if code == 0xC0:
        (program,) = data
        result['program'] = program + 1
    elif code == 0xF1:
        (packed,) = data
        result.update(piece=packed >> 4, value=packed & 15)
    elif code in (0xE0, 0xF2):
        low, high = data
        number = low | high << 7
        if code == 0xE0:
            result['value'] = number - 8192
        else:
            result['position'] = number
    else:
        result.update(zip(MESSAGES[name][1], data, strict=True))
    return result


def decode_parameter(message: bytes) -> dict[str, object]:
    if len(message) not in (6, 15, 18):
        raise ValueError('invalid MIDI parameter transaction length')
    channel = (message[0] & 15) + 1
    if len(message) == 6:
        result: dict[str, object] = {
            'type': 'control-change',
            'channel': channel,
            'controller': message[1],
            'value': message[2] * 128 + message[5],
            'resolution': 14,
        }
        value = models.ControlChange.model_validate(result)
    else:
        result = {
            'type': 'rpn' if message[1] == 101 else 'nrpn',
            'channel': channel,
            'parameter': message[2] * 128 + message[5],
            'value': message[8]
            if len(message) == 15
            else message[8] * 128 + message[11],
        }
        if len(message) == 15:
            result['resolution'] = 7
        value = (
            models.Rpn.model_validate(result)
            if result['type'] == 'rpn'
            else models.Nrpn.model_validate(result)
        )
    if encode(value) != message:
        raise ValueError('invalid MIDI parameter transaction')
    return result


MESSAGES = {
    'note-off': (0x80, ('note', 'velocity')),
    'note-on': (0x90, ('note', 'velocity')),
    'polyphonic-key-pressure': (0xA0, ('note', 'pressure')),
    'control-change': (0xB0, ('controller', 'value')),
    'program-change': (0xC0, ()),
    'channel-pressure': (0xD0, ('pressure',)),
    'pitch-bend': (0xE0, ()),
    'time-code-quarter-frame': (0xF1, ()),
    'song-position-pointer': (0xF2, ()),
    'song-select': (0xF3, ('song',)),
    'tune-request': (0xF6, ()),
    'timing-clock': (0xF8, ()),
    'start': (0xFA, ()),
    'continue': (0xFB, ()),
    'stop': (0xFC, ()),
    'active-sensing': (0xFE, ()),
    'system-reset': (0xFF, ()),
}
TYPES = {s: n for n, (s, _) in MESSAGES.items()}
