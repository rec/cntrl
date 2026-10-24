"""Export the authoritative pedal document as self-contained resolved TOML."""

import struct
from zlib import crc32

import tomlkit

from .compiler import HEADER


def export(data: bytes) -> str:
    if len(data) < 116:
        raise ValueError('truncated setup')
    magic, version, chord, length, ni, nc, np, flags, crc, timeout = HEADER.unpack_from(
        data
    )
    if (
        magic != b'CSW1'
        or version != 7
        or length != len(data)
        or crc32(data[28:]) != crc
    ):
        raise ValueError('invalid setup header/checksum')
    meta = 76 + ni * 68 + nc * 74 + np * 82
    if meta + 40 > len(data) or data[meta : meta + 4] != b'CMTA':
        raise ValueError('missing setup metadata')
    nb = struct.unpack_from('<H', data, meta + 4)[0]
    if meta + 40 + ni * 64 + nb * 8 + np * 4 != len(data):
        raise ValueError('invalid setup metadata size')

    def text(offset: int, width: int) -> str:
        return data[offset : offset + width].split(b'\0')[0].decode('ascii')

    def color(offset: int) -> str:
        value = struct.unpack_from('<H', data, offset)[0]
        red, green, blue = (value >> 11) * 8, ((value >> 5) & 63) * 4, (value & 31) * 8
        return f'0x{red:02X}{green:02X}{blue:02X}'

    result: dict[str, object] = {
        'chord_ms': chord,
        'brightness': flags & 255,
        'transition_ms': struct.unpack_from('<H', data, meta + 6)[0] or 1000,
        'splash': bool(flags >> 8),
        'screensaver_timeout': timeout // 1000,
        'font_name': text(meta + 8, 32),
        'font_size': data[29],
        'pad': data[30],
    }
    result['screens'] = {
        n: {
            'foreground_color': color(32 + i * 16),
            'background_color': color(34 + i * 16),
            'half': color(40 + i * 16),
        }
        for i, n in enumerate(('undo', 'sent', 'next'))
    }
    instruments: dict[str, object] = {}
    setups: list[dict[str, object]] = []
    aliases: list[str] = []
    for i in range(ni):
        offset = 76 + i * 68
        identity = meta + 40 + i * 64
        alias = text(offset, 32)
        aliases.append(alias)
        instrument, setup = text(identity, 32), text(identity + 32, 32)
        instruments[alias] = {
            'instrument': instrument,
            'setup': setup,
            'channel': data[offset + 64] + 1,
            'output': text(offset + 32, 32),
        }
        banks: dict[str, object] = {}
        for j in range(nb):
            index, bank, msb, lsb = struct.unpack_from(
                '<HIBB', data, meta + 40 + ni * 64 + j * 8
            )
            if index == i:
                banks[str(bank)] = {
                    k: v for k, v in (('msb', msb), ('lsb', lsb)) if v != 255
                }
        entry: dict[str, object] = {
            'instrument': instrument,
            'name': setup,
            'banks': banks,
        }
        if entry not in setups:
            setups.append(entry)
    result['instruments'] = instruments
    result['definitions'] = {'setups': setups}
    categories: dict[str, object] = {}
    for i in range(nc):
        offset = 76 + ni * 68 + i * 74
        start, count = struct.unpack_from('<HH', data, offset + 64)
        patches: list[dict[str, object]] = []
        for j in range(start, start + count):
            p = 76 + ni * 68 + nc * 74 + j * 82
            index = struct.unpack_from('<H', data, p)[0]
            bank = struct.unpack_from('<I', data, meta + 40 + ni * 64 + nb * 8 + j * 4)[
                0
            ]
            patch: dict[str, object] = {
                'instrument': aliases[index],
                'program': data[p + 4] + 1,
                'name': text(p + 12, 64),
            }
            if bank != 0xFFFFFFFF:
                patch['bank'] = bank
            if data[p + 7] != 255:
                patch['volume'] = data[p + 7]
            if data[p + 8] != 255:
                patch['pan'] = data[p + 8] - 64
            patches.append(patch)
        categories[text(offset, 64)] = patches
    result['patches'] = categories
    return tomlkit.dumps(result)
