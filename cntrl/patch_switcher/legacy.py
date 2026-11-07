"""Offline normalization of saved setup formats 20 through 33 to format 34.

Only migration tools use this module. Firmware and ordinary setup loading accept
the current format exclusively. Musical graph nodes and prepared display assets
are retained; older metadata receives the defaults of its original application.
"""

import struct
from zlib import crc32

import tomlkit

from . import compiler, display, models, page_settings


def upgrade(
    data: bytes,
    target: models.Target,
    setup_names: list[str],
    movie_keys: list[int] | None = None,
) -> bytes:
    if len(data) < 76:
        raise ValueError('truncated historical setup')
    header = list(compiler.HEADER.unpack_from(data))
    magic, version, _, length, ni, nc, np, flags, checksum, _ = header
    if (
        magic != b'CSW1'
        or not 20 <= version <= 34
        or length != len(data)
        or crc32(data[28:]) != checksum
    ):
        raise ValueError('invalid historical setup header/checksum')
    if version == 34:
        return data
    meta = 76 + ni * 68 + nc * 68 + np * 82
    old_metadata = metadata_format(version)
    values = list(old_metadata.unpack_from(data, meta))
    if values[0] != b'CMTA':
        raise ValueError('missing historical setup metadata')
    nb, transition = values[1:3]
    if version < 30 and not transition:
        transition = 75
    old_cursor = meta + old_metadata.size + ni * 64 + nb * 8 + np * 4
    settings = models.Setup()
    words = page_settings.pack(settings, target)
    period, minimum, maximum = values[3:6] if version >= 22 else (4000, 64, 128)
    repeat = values[6] if version >= 23 else 200
    delay = values[7] if version >= 25 else 0
    word_offset = 8 if version >= 25 else 7
    if version >= 24:
        count = 23 if version >= 27 else 22
        words[:count] = values[word_offset : word_offset + count]
        if version < 27:
            words[22] = words[15]
        if version < 26:
            # The former fixed-duration flash was replaced by a cycle fraction.
            words[8] = 400
            # Old titles occupied one text row. The current heading band is
            # taller; retain text/ring sizes and fit only the flasher diameter.
            available = min(
                target.display_width - 2 * words[10] - 2 * words[13],
                target.display_height
                - words[11] * 3 // 2
                - 4
                - words[12]
                - 2 * words[10]
                - 2 * words[13],
            )
            if available < 1:
                raise ValueError(
                    'historical tap layout cannot fit the target heading band'
                )
            words[9] = min(words[9], available)
    # Formats predating configurable port roles used active MIDI outputs.
    modes = values[word_offset + 23] if version >= 29 else bytes([1] * 4)
    if version >= 30:
        at = word_offset + 24
        vertical, send, patches, cycles = values[at : at + 4]
    else:
        vertical, send, patches, cycles = 200, 0, 0, 2
    fast = values[-3] if version >= 33 else values[-1] if version >= 32 else 0
    app_memory = values[-2] if version >= 33 else 0
    signature = crc32(
        b''.join(
            n.lower().encode('ascii').ljust(64, b'\0')
            for n in sorted(setup_names, key=str.lower)
        )
    )
    metadata = compiler.METADATA.pack(
        b'CMTA',
        nb,
        transition,
        period,
        minimum,
        maximum,
        repeat,
        delay,
        *words,
        modes,
        vertical,
        send,
        patches,
        cycles,
        fast,
        app_memory,
        signature,
    )
    cursor = optional_sections_end(data, old_cursor, flags)
    prefix = data[old_cursor:cursor]
    if version >= 31:
        apps, pages = data[cursor + 4 : cursor + 6]
        end = cursor + 6 + apps * 35 + pages * 2
        if (
            data[cursor : cursor + 4] != b'CLAY'
            or apps > 4
            or pages > 32
            or end > len(data)
        ):
            raise ValueError('invalid historical app layout')
        app_layout = data[cursor:end]
        cursor = end
    else:
        # Older application instances were implicit. Preserve the original page
        # cycle and the three-window wire record, ready for the next conversion.
        pages = bytes(
            v for p in ([1, *range(2, 10), 0] if version >= 24 else []) for v in (p, 1)
        )
        app_layout = (
            b'CLAY'
            + bytes((1, len(pages) // 2))
            + b'patch-switcher'.ljust(32, b'\0')
            + bytes((2, 1, 0))
            + pages
        )
    if data[cursor : cursor + 4] != b'CNAM':
        raise ValueError('missing historical name metadata')
    names_start = cursor
    cursor += 4
    for _ in range(np * 3):
        size = struct.unpack_from('<H', data, cursor)[0]
        cursor += 2 + (0 if size == 65535 else size)
    graph, source = upgrade_graph(data, cursor, version, setup_names)
    before = bytearray(data[28:meta])
    if version == 20:
        before[:48] = before[32:48] + before[16:32] + before[:16]
        start = 48 + ni * 68 + nc * 68
        for i in range(np):
            at = start + i * 82 + 76
            before[at : at + 6] = (
                before[at + 4 : at + 6] + before[at + 2 : at + 4] + before[at : at + 2]
            )
    startup = list(movie_keys or []) if flags & 256 else []
    idle = startup if header[9] else []
    movies = struct.pack('<4sHH', b'CMOV', len(startup), len(idle)) + b''.join(
        struct.pack('<I', k) for k in startup + idle
    )
    body = (
        bytes(before)
        + metadata
        + data[meta + old_metadata.size : old_cursor]
        + prefix
        + app_layout
        + movies
        + data[names_start:cursor]
        + graph
        + source
    )
    header[1] = 34
    header[3] = 28 + len(body)
    # Both USB transports were enabled unconditionally before format 28.
    header[7] = flags | (0x1800 if version < 28 else 0)
    # Pre-list formats used one global movie. The migration tool supplies its
    # saved identity explicitly because historical setup bytes never stored it.
    header[7] = (header[7] & ~256) | (256 if startup else 0)
    header[8] = crc32(body)
    return compiler.HEADER.pack(*header) + body


def metadata_format(version: int) -> struct.Struct:
    if version <= 21:
        return struct.Struct('<4sHH')
    if version == 22:
        return struct.Struct('<4sHHHBB')
    if version == 23:
        return struct.Struct('<4sHHHBBH')
    if version == 24:
        return struct.Struct('<4sHHHBBH22H')
    if version <= 26:
        return struct.Struct('<4sHHHBBHH22H')
    if version <= 28:
        return struct.Struct('<4sHHHBBHH23H')
    if version == 29:
        return struct.Struct('<4sHHHBBHH23H4s')
    if version <= 31:
        return struct.Struct('<4sHHHBBHH23H4sH3B')
    if version == 32:
        return struct.Struct('<4sHHHBBHH23H4sH4B')
    return compiler.METADATA


def optional_sections_end(data: bytes, cursor: int, flags: int) -> int:
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
    if flags & compiler.GESTURES_FLAG:
        cursor += compiler.GESTURES.size * 5
    for tag, size in (
        (b'CHOK', compiler.SWITCH_HOOKS.size),
        (b'CSHF', compiler.SHIFTED_ACTIONS.size),
    ):
        if data[cursor : cursor + 4] == tag:
            cursor += 4 + size * 5
    return cursor


def upgrade_graph(
    data: bytes, cursor: int, version: int, setup_names: list[str]
) -> tuple[bytes, bytes]:
    magic, length, count, *_ = compiler.GRAPH.unpack_from(data, cursor)
    if magic != b'CPAT' or cursor + length > len(data):
        raise ValueError('invalid historical patch graph')
    original = data[cursor : cursor + length]
    if version >= 33:
        return original, data[cursor + length :]
    offsets = [
        struct.unpack_from('<I', original, compiler.GRAPH.size + i * 4)[0]
        for i in range(count)
    ]
    start = offsets[0] if offsets else length
    prefix = bytearray(original[:start])
    nodes = []
    authored = display.read_source(data, cursor)
    # The former random palette is already resolved into the retained display
    # style records. Current authoring uses a single base background colour.
    authored.pop('background_colors', None)
    if isinstance(authored.get('pages'), dict):
        authored['patch_views'] = authored.pop('pages')
    if isinstance(windows := authored.get('windows'), dict):
        for window in windows.values():
            if isinstance(window, dict) and 'page' in window:
                window['view'] = window.pop('page')
    if isinstance(tags := authored.get('tags'), dict):
        authored['tag_styles'] = {
            n: v['style']
            for n, v in tags.items()
            if isinstance(v, dict) and v.get('style') is not None
        }
        authored['tags'] = []
    metadata = authored.setdefault('patch_metadata', {})
    if not isinstance(metadata, dict):
        raise ValueError('invalid historical patch metadata')
    indices = {n.lower(): i for i, n in enumerate(sorted(setup_names, key=str.lower))}
    for i, offset in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < count else length
        node = original[offset:end]
        if len(node) < 4 or struct.unpack_from('<H', node, 2)[0] + 4 != len(node):
            raise ValueError('invalid historical graph node length')
        if node[0] == 6:
            if len(node) != 132:
                raise ValueError('invalid historical reference node')
            name = node[4:68].split(b'\0')[0].decode('ascii')
            index = 65535
            if name:
                if name.lower() not in indices:
                    raise ValueError(
                        f'historical setup reference {name!r} '
                        'is absent from the setup table'
                    )
                index = indices[name.lower()]
                item = metadata.setdefault(str(i), {})
                if not isinstance(item, dict):
                    raise ValueError('invalid historical reference metadata')
                item['_document'] = name
            node = node[:2] + struct.pack('<HH', 66, index) + node[68:132]
        nodes.append(node)
    position = len(prefix)
    for i, node in enumerate(nodes):
        struct.pack_into('<I', prefix, compiler.GRAPH.size + i * 4, position)
        position += len(node)
    struct.pack_into('<I', prefix, 4, position)
    tail = bytearray(data[cursor + length :])
    text = tomlkit.dumps(authored).encode('utf-8')
    old_size = display.HEADER.unpack_from(tail)[-1]
    struct.pack_into('<I', tail, display.HEADER.size - 4, len(text))
    return bytes(prefix) + b''.join(nodes), bytes(tail[:-old_size]) + text
