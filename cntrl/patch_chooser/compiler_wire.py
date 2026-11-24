"""Wire fields and encoding helpers shared by setup compilers and readers."""

import struct
from zlib import crc32

from . import banked, models


def pack_name(text: str | None, path: str, errors: list[str]) -> bytes:
    if text is None:
        return struct.pack('<H', 65535)
    encoded = text.encode('utf-8')
    if len(encoded) >= 65535:
        errors.append(f'{path}: name exceeds storage capacity')
    return struct.pack('<H', min(len(encoded), 65534)) + encoded[:65534]


def fixed_text(
    value: str,
    width: int,
    target: models.Target,
    context: str,
    errors: list[str],
    unicode: bool = False,
) -> bytes:
    unsupported = list(
        dict.fromkeys(
            c
            for c in value
            if ord(c) < 32
            or ord(c) == 127
            or (not unicode and (c not in target.characters or ord(c) > 126))
        )
    )
    if unsupported:
        errors.append(f'{context}: unsupported characters {unsupported!r}')
    encoded = (
        value.encode('utf-8') if unicode else value.encode('ascii', errors='replace')
    )
    if not value or len(encoded) >= width:
        errors.append(f'{context}: text must contain 1..{width - 1} characters')
    # Placeholder bytes let later entries be checked. Errors prevent emission.
    return encoded[: width - 1]


def optional_byte(value: int | None) -> int:
    return 255 if value is None else value


GRAPH = struct.Struct('<4sIHHHH')
SWITCH = struct.Struct('<BBHH64sH')
GESTURES_FLAG = 1 << 14
GESTURES = struct.Struct('<16H')
SWITCH_HOOKS = struct.Struct('<HH?')
SHIFTED_ACTIONS = struct.Struct('<14H4H')
BINDING_HOOKS_OFFSET = SWITCH.size + GESTURES.size
BINDING_SHIFTED_OFFSET = BINDING_HOOKS_OFFSET + SWITCH_HOOKS.size
BINDING_RECORD_SIZE = BINDING_SHIFTED_OFFSET + SHIFTED_ACTIONS.size
GESTURE_EVENTS = (
    'release',
    'long_press',
    'long_release',
    'double_tap',
    'double_release',
    'repeat',
    'long_double_tap',
    'long_double_release',
    'triple_tap',
    'triple_release',
    'long_triple_tap',
    'long_triple_release',
)
GESTURE_TIMINGS = ('debounce', 'long_delay', 'double_delay', 'repeat_interval')
NAMED = struct.Struct('<64sH')
STATE_OPERATIONS = {
    n: i + 1
    for i, n in enumerate(
        (
            'brightness',
            'usb-midi-interface',
            'next-patch',
            'previous-patch',
            'next-patch-cycle',
            'previous-patch-cycle',
            'activate-selection',
            'undo',
            '',
            'back',
            'shift',
            'unshift',
            'next-song-step',
            'next-bank',
            'previous-bank',
            'next-bank-page',
            'previous-bank-page',
            'previous-song-step',
        )
    )
    if n
}


def pack_document(
    show: models.Setup,
    body: bytes,
    *,
    instruments: int,
    cycles: int,
    roots: int,
    flags: int,
) -> bytes:
    result = (
        HEADER.pack(
            b'CSW1',
            DOCUMENT_VERSION,
            round(show.navigation.hold_delay * 1000),
            HEADER.size + len(body),
            instruments,
            cycles,
            roots,
            show.brightness
            | int(bool(show.splash_movie)) << 8
            | int(show.is_midi_interface) << 9
            | int(show.animation_color_cycle) << 10
            | int(show.usb_device_midi) << 11
            | int(show.usb_host_midi) << 12
            | flags,
            crc32(body),
            show.screensaver_timeout * 1000,
        )
        + body
    )
    if extra_ram := banked.estimate_ram(show.pages, len(result) + 28):
        result = bytearray(result)
        address = 76 + instruments * 68 + cycles * 68 + roots * 82 + METADATA.size - 8
        struct.pack_into(
            '<I',
            result,
            address,
            struct.unpack_from('<I', result, address)[0] + extra_ram,
        )
        struct.pack_into('<I', result, 20, crc32(result[28:]))
    return bytes(result)


HEADER = struct.Struct('<4sHHIHHHHII')
METADATA = struct.Struct('<4sHHHBBHH23H4sH6BII')
DOCUMENT_VERSION = 48
LOOPER_ACTIONS = (
    'record',
    'play-stop',
    'undo',
    'clear',
    'save',
    'load',
    'reverse',
    'direction',
)
SCROLL_DIRECTIONS = ('right', 'left', 'up', 'down')
TRS_MODES = ('off', 'midi-a', 'midi-b', 'expression', 'aux')
