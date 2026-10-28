"""Export the authoritative pedal document as self-contained resolved TOML."""

import struct
from typing import cast
from zlib import crc32

import tomlkit

from .compiler import DOCUMENT_VERSION, HEADER, METADATA
from .display import read_source


def export(data: bytes) -> str:
    if len(data) < 116:
        raise ValueError('truncated setup')
    magic, version, chord, length, ni, nc, np, flags, crc, timeout = HEADER.unpack_from(
        data
    )
    if (
        magic != b'CSW1'
        or version != DOCUMENT_VERSION
        or flags >> 10
        or length != len(data)
        or crc32(data[28:]) != crc
    ):
        raise ValueError('invalid setup header/checksum')
    meta = 76 + ni * 68 + nc * 68 + np * 82
    if meta + METADATA.size > len(data) or data[meta : meta + 4] != b'CMTA':
        raise ValueError('missing setup metadata')
    nb = struct.unpack_from('<H', data, meta + 4)[0]
    names_offset = meta + METADATA.size + ni * 64 + nb * 8 + np * 4
    if data[names_offset : names_offset + 4] != b'CNAM':
        raise ValueError('invalid setup metadata size')
    names_offset += 4
    names: list[list[str | None]] = []
    for _ in range(np):
        fields: list[str | None] = []
        for _ in range(3):
            if names_offset + 2 > len(data):
                raise ValueError('truncated patch-name metadata')
            size = struct.unpack_from('<H', data, names_offset)[0]
            names_offset += 2
            if size == 65535:
                fields.append(None)
            else:
                if names_offset + size > len(data):
                    raise ValueError('truncated patch-name metadata')
                fields.append(data[names_offset : names_offset + size].decode('utf-8'))
                names_offset += size
        names.append(fields)
    source = read_source(data, names_offset)
    patch_styles = cast(dict[str, object], source.pop('patch_styles'))
    program_styles = cast(dict[str, object], source.pop('program_styles'))
    patch_categories = cast(dict[str, object], source.pop('patch_categories'))
    program_categories = cast(dict[str, object], source.pop('program_categories'))

    def text(offset: int, width: int) -> str:
        return data[offset : offset + width].split(b'\0')[0].decode('utf-8')

    result: dict[str, object] = {
        'chord_ms': chord,
        'brightness': flags & 255,
        'splash': bool(flags & 256),
        'usb_midi_interface': bool(flags & 512),
        'screensaver_timeout': timeout // 1000,
    }
    if duration := struct.unpack_from('<H', data, meta + 6)[0]:
        result['transition_ms'] = duration
    result.update(source)
    instruments: dict[str, object] = {}
    setups: list[dict[str, object]] = []
    aliases: list[str] = []
    for i in range(ni):
        offset = 76 + i * 68
        identity = meta + METADATA.size + i * 64
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
                '<HIBB', data, meta + METADATA.size + ni * 64 + j * 8
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
    programs: dict[tuple[str, str, int, int | None], dict[str, object]] = {}
    categories: dict[str, object] = {}
    for i in range(nc):
        offset = 76 + ni * 68 + i * 68
        start, count = struct.unpack_from('<HH', data, offset + 64)
        patches: list[dict[str, object]] = []
        for j in range(start, start + count):
            p = 76 + ni * 68 + nc * 68 + j * 82
            index = struct.unpack_from('<H', data, p)[0]
            bank = struct.unpack_from(
                '<I', data, meta + METADATA.size + ni * 64 + nb * 8 + j * 4
            )[0]
            patch: dict[str, object] = {
                'instrument': aliases[index],
                'program': data[p + 4] + 1,
            }
            name, program_display_name, patch_display_name = names[j]
            identity = meta + METADATA.size + index * 64
            program: dict[str, object] = {
                'instrument': text(identity, 32),
                'setup': text(identity + 32, 32),
                'program': data[p + 4] + 1,
            }
            if name is not None:
                program['name'] = name
            if program_display_name is not None:
                program['display_name'] = program_display_name
            if patch_display_name is not None:
                patch['display_name'] = patch_display_name
            if str(j) in patch_styles:
                patch['style'] = patch_styles[str(j)]
            if str(j) in program_styles:
                program['style'] = program_styles[str(j)]
            if str(j) in patch_categories:
                patch['categories'] = patch_categories[str(j)]
            if str(j) in program_categories:
                program['categories'] = program_categories[str(j)]
            if bank != 0xFFFFFFFF:
                patch['bank'] = bank
                program['bank'] = bank
            key = (
                text(identity, 32),
                text(identity + 32, 32),
                data[p + 4] + 1,
                None if bank == 0xFFFFFFFF else bank,
            )
            if key in programs and programs[key] != program:
                raise ValueError(f'conflicting program names for {key}')
            programs[key] = program
            if data[p + 7] != 255:
                patch['volume'] = data[p + 7]
            if data[p + 8] != 255:
                patch['pan'] = data[p + 8] - 64
            patches.append(patch)
        categories[text(offset, 64)] = patches
    result['patches'] = categories
    result['definitions'] = {'setups': setups, 'programs': list(programs.values())}
    return tomlkit.dumps(result)
