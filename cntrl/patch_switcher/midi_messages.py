"""Translate named MIDI 1.0 messages to and from a single wire message."""

from . import models


def encode(value: models.OutputMessage) -> bytes:
    if isinstance(value, models.SysEx):
        return bytes((0xF0, *value.data, 0xF7))
    status, fields = MESSAGES[value.type]
    if isinstance(value, models.ChannelMessage):
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
