"""Offline conversion of saved setups and their independently stored assets."""

import struct
from zlib import crc32

import tomlkit

from ..animation.asset import Geometry, convert_legacy, inspect_asset
from . import (
    compiler,
    display,
    inputs,
    layout,
    legacy,
    midi_endpoints,
    models,
    page_settings,
    resolved,
    songs,
)


def upgrade_movies(
    assets: dict[int, bytes], geometry: Geometry
) -> tuple[dict[int, bytes], dict[int, int]]:
    converted: dict[int, bytes] = {}
    keys: dict[int, int] = {}
    for key, original in assets.items():
        if crc32(original) != key:
            raise ValueError(f'{key:08x}: movie checksum differs from its saved key')
        data = (
            convert_legacy(original, geometry)
            if original[:6] == b'CNSA\x01\x00'
            else original
        )
        inspect_asset(data)
        replacement = crc32(data)
        if replacement in converted and converted[replacement] != data:
            raise ValueError('converted movie checksum collision')
        converted[replacement] = data
        keys[key] = replacement
    return converted, keys


def upgrade_setup(
    data: bytes, target: models.Target, movie_key_map: dict[int, int]
) -> bytes:
    """Convert format 34 without recompiling any musical or edited graph nodes."""
    if len(data) < 76:
        raise ValueError('truncated legacy setup')
    magic, version, _, length, ni, nc, np, flags, checksum, _ = (
        compiler.HEADER.unpack_from(data)
    )
    if (
        magic != b'CSW1'
        or version != 34
        or length != len(data)
        or crc32(data[28:]) != checksum
    ):
        raise ValueError('expected a valid format-34 setup')
    meta = 76 + ni * 68 + nc * 68 + np * 82
    cursor, _ = _switch_sections(data, meta, ni, np, flags)
    section, old_end = layout.upgrade_legacy_section(
        data, cursor, [d.id for d in target.displays]
    )
    if data[old_end : old_end + 4] != b'CMOV' or old_end + 8 > len(data):
        raise ValueError('missing legacy movie metadata')
    startup, idle = struct.unpack_from('<HH', data, old_end + 4)
    movie_end = old_end + 8 + 4 * (startup + idle)
    if movie_end > len(data):
        raise ValueError('truncated legacy movie references')
    keys: list[int] = []
    for i in range(startup + idle):
        key = struct.unpack_from('<I', data, old_end + 8 + i * 4)[0]
        if key not in movie_key_map:
            raise ValueError(
                f'{key:08x}: referenced movie is absent from the conversion'
            )
        keys.append(movie_key_map[key])
    movies = struct.pack('<4sHH', b'CMOV', startup, idle) + b''.join(
        struct.pack('<I', k) for k in keys
    )
    modes = data[
        meta + compiler.METADATA.size - 18 : meta + compiler.METADATA.size - 14
    ]
    configured = target.model_copy(
        update={
            'midi_endpoints': [
                e.model_copy(
                    update={
                        'output': e.output
                        and (e.port is None or modes[e.port] in (1, 2))
                    }
                )
                for e in target.midi_endpoints
            ]
        }
    )
    result = _header(
        data[:cursor]
        + section
        + movies
        + midi_endpoints.pack(configured)
        + inputs.pack(target)
        + data[movie_end:]
    )
    graph = resolved.graph_offset(result)
    result = _permute_switch_records(result, meta, ni, np, flags, graph)
    display_offset = graph + struct.unpack_from('<I', result, graph + 4)[0]
    display.read_source(result, graph)
    source_size = struct.unpack_from('<I', result, display_offset + 12)[0]
    source = tomlkit.parse(result[len(result) - source_size :].decode('utf-8'))
    source['splash_movie'] = [f'movies/{k:08x}.cnsa' for k in keys[:startup]]
    source['idle_movie'] = [f'movies/{k:08x}.cnsa' for k in keys[startup:]]
    arrangement = layout.unpack(section, 0)
    source.pop('apps', None)
    source['pages'] = arrangement['pages']
    authored = tomlkit.dumps(source).encode('utf-8')
    rewritten = bytearray(result[: len(result) - source_size] + authored)
    struct.pack_into('<I', rewritten, display_offset + 12, len(authored))
    result = _header(bytes(rewritten))
    resolved.graph_offset(result)
    return result


def upgrade_songs(
    data: bytes,
    target: models.Target,
    movie_key_map: dict[int, int],
    setup_names: list[str],
    legacy_movie_keys: list[int] | None = None,
) -> bytes:
    """Keep song/setlist ordering and replace only embedded setup containers."""
    if len(data) < 12:
        raise ValueError('truncated song library')
    song_count, set_count = struct.unpack_from('<II', data)
    cursor = 8
    for _ in range(song_count):
        header = cursor + songs.SELECTION.size
        if header + songs.SONG.size > len(data):
            raise ValueError('truncated song header')
        count = struct.unpack_from('<I', data, header + 64)[0]
        if (
            not count
            or count > (len(data) - cursor - songs.SONG.size) // songs.SELECTION.size
        ):
            raise ValueError('invalid song selection count')
        cursor += songs.SONG.size + count * songs.SELECTION.size
    for _ in range(set_count):
        if cursor + songs.SET.size > len(data):
            raise ValueError('truncated setlist header')
        count = struct.unpack_from('<I', data, cursor + 64)[0]
        cursor += songs.SET.size + count * 64
        if cursor > len(data):
            raise ValueError('truncated setlist entries')
    if data[cursor : cursor + 4] != b'CSP5':
        raise ValueError('missing song setup containers')
    cursor += 4
    result = bytearray(data[:cursor])
    while cursor < len(data):
        if cursor + 4 > len(data):
            raise ValueError('truncated embedded setup length')
        length = struct.unpack_from('<I', data, cursor)[0]
        cursor += 4
        if length < 76 or length > len(data) - cursor:
            raise ValueError('invalid embedded setup length')
        source = data[cursor : cursor + length]
        cursor += length
        version = struct.unpack_from('<H', source, 4)[0]
        if version < 34:
            source = legacy.upgrade(source, target, setup_names, legacy_movie_keys)
        if version <= 34:
            source = upgrade_setup(source, target, movie_key_map)
        else:
            resolved.graph_offset(source)
        result.extend(struct.pack('<I', len(source)))
        result.extend(source)
    songs.decode(bytes(result))
    return bytes(result)


def _header(data: bytes) -> bytes:
    result = bytearray(data)
    struct.pack_into('<H', result, 4, compiler.DOCUMENT_VERSION)
    struct.pack_into('<I', result, 8, len(result))
    struct.pack_into('<I', result, 20, crc32(result[28:]))
    return bytes(result)


def _permute_switch_records(
    data: bytes, meta: int, instruments: int, patches: int, flags: int, graph: int
) -> bytes:
    _, sections = _switch_sections(data, meta, instruments, patches, flags)
    nodes, switches = struct.unpack_from('<HH', data, graph + 8)
    records = graph + compiler.GRAPH.size + nodes * 4 + patches * 2
    physical = [data[records + i * compiler.SWITCH.size] for i in range(switches)]
    if switches > 5 or any(b >= 5 for b in physical):
        raise ValueError('invalid historical switch assignment')
    result = bytearray(data)
    for offset, record, default in sections:
        old = data[offset : offset + record.size * 5]
        result[offset : offset + record.size * 5] = default * 5
        for slot, button in enumerate(physical):
            result[offset + slot * record.size : offset + (slot + 1) * record.size] = (
                old[button * record.size : (button + 1) * record.size]
            )
    return _header(bytes(result))


def _switch_sections(
    data: bytes, meta: int, instruments: int, patches: int, flags: int
) -> tuple[int, list[tuple[int, struct.Struct, bytes]]]:
    if data[meta : meta + 4] != b'CMTA':
        raise ValueError('missing legacy setup metadata')
    mappings = struct.unpack_from('<H', data, meta + 4)[0]
    cursor = (
        meta + compiler.METADATA.size + instruments * 64 + mappings * 8 + patches * 4
    )
    if flags & page_settings.EXPRESSION_FLAG:
        cursor += page_settings.EXPRESSION.size * 4
    if flags & page_settings.CLOCK_FLAG:
        cursor += page_settings.CLOCK.size
    if data[cursor : cursor + 4] == b'CKBD':
        cursor += (
            6
            + struct.unpack_from('<H', data, cursor + 4)[0]
            * page_settings.KEYBOARD.size
        )
    sections: list[tuple[int, struct.Struct, bytes]] = []
    if flags & compiler.GESTURES_FLAG:
        sections.append(
            (
                cursor,
                compiler.GESTURES,
                compiler.GESTURES.pack(*([65535] * 6), 0, 0, 0, 0),
            )
        )
        cursor += compiler.GESTURES.size * 5
    if data[cursor : cursor + 4] == b'CHOK':
        sections.append(
            (
                cursor + 4,
                compiler.SWITCH_HOOKS,
                compiler.SWITCH_HOOKS.pack(65535, 65535, False),
            )
        )
        cursor += 29
    if data[cursor : cursor + 4] == b'CSHF':
        sections.append(
            (
                cursor + 4,
                compiler.SHIFTED_ACTIONS,
                compiler.SHIFTED_ACTIONS.pack(*([65535] * 8), 0, 0, 0, 0),
            )
        )
        cursor += 124
    return cursor, sections
