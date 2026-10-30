"""Export editable authoring from the authoritative compiled Patch graph."""

import struct
from typing import cast
from zlib import crc32

import tomlkit

from .compiler import (
    DOCUMENT_VERSION,
    GRAPH,
    HEADER,
    METADATA,
    NAMED,
    STATE_OPERATIONS,
    SWITCH,
)
from .display import read_source


def graph_offset(data: bytes) -> int:
    """Locate the graph following variable name metadata."""
    if len(data) < 76:
        raise ValueError('truncated setup')
    magic, version, _, length, ni, nc, np, flags, crc, _ = HEADER.unpack_from(data)
    if (
        magic != b'CSW1'
        or version != DOCUMENT_VERSION
        or flags >> 10
        or length != len(data)
        or crc32(data[28:]) != crc
    ):
        raise ValueError('invalid setup header/checksum')
    meta = 76 + ni * 68 + nc * 68 + np * 82
    if data[meta : meta + 4] != b'CMTA':
        raise ValueError('missing setup metadata')
    nb = struct.unpack_from('<H', data, meta + 4)[0]
    cursor = meta + METADATA.size + ni * 64 + nb * 8 + np * 4
    if data[cursor : cursor + 4] != b'CNAM':
        raise ValueError('missing name metadata')
    cursor += 4
    for _ in range(np * 3):
        if cursor + 2 > len(data):
            raise ValueError('truncated name metadata')
        size = struct.unpack_from('<H', data, cursor)[0]
        cursor += 2 + (0 if size == 65535 else size)
    if cursor + GRAPH.size > len(data) or data[cursor : cursor + 4] != b'CPAT':
        raise ValueError('missing Patch graph')
    return cursor


def patch(data: bytes, node: int) -> dict[str, object]:
    """Decode any graph node into the common tagged Patch authoring table."""
    offset = graph_offset(data)
    source = read_source(data, offset)
    metadata = cast(dict[str, object], source.get('patch_metadata', {}))
    return _patch(data, offset, node, metadata, set())


def export(data: bytes) -> str:
    offset = graph_offset(data)
    _, _, chord, _, ni, nc, np, flags, _, timeout = HEADER.unpack_from(data)
    meta = 76 + ni * 68 + nc * 68 + np * 82
    nb = struct.unpack_from('<H', data, meta + 4)[0]
    source = read_source(data, offset)
    metadata = cast(dict[str, object], source.pop('patch_metadata', {}))
    source.pop('program_styles', None)
    source.pop('program_tags', None)
    source.pop('patch_styles', None)
    source.pop('patch_tags', None)
    authored_switches = cast(dict[str, object], source.pop('switches', {}))
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
    for i in range(ni):
        address = 76 + i * 68
        identity = meta + METADATA.size + i * 64
        instrument, setup = _text(data, identity, 32), _text(data, identity + 32, 32)
        instruments[_text(data, address, 32)] = {
            'instrument': instrument,
            'setup': setup,
            'channel': data[address + 64] + 1,
            'output': _text(data, address + 32, 32),
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
        value: dict[str, object] = {
            'instrument': instrument,
            'name': setup,
            'banks': banks,
        }
        if value not in setups:
            setups.append(value)
    result['instruments'] = instruments
    _, size, nodes, ns, nr, nn = GRAPH.unpack_from(data, offset)
    cursor = offset + GRAPH.size + nodes * 4
    roots = list(struct.unpack_from(f'<{nr}H', data, cursor))
    cursor += nr * 2
    switch_start = cursor
    cursor += ns * SWITCH.size
    named: dict[str, object] = {}
    named_nodes: dict[str, int] = {}
    for _ in range(nn):
        key, index = NAMED.unpack_from(data, cursor)
        cursor += NAMED.size
        identity = key.split(b'\0')[0].decode('ascii')
        named_nodes[identity] = index
        named[identity] = _patch(data, offset, index, metadata, set())
    result['patches'] = named
    patch_cycles: dict[str, object] = {}
    for i in range(nc):
        address = 76 + ni * 68 + i * 68
        start, count = struct.unpack_from('<HH', data, address + 64)
        values: list[dict[str, object]] = []
        for position in range(start, start + count):
            root = roots[position]
            value = _patch(data, offset, root, metadata, set())
            if value['type'] == 'reference' and _edited_program(
                data, offset, root, metadata, named_nodes
            ):
                cache = 76 + ni * 68 + nc * 68 + position * 82
                value['display_name'] = _text(data, cache + 12, 64)
            values.append(value)
        patch_cycles[_text(data, address, 64)] = values
    result['patch_cycles'] = patch_cycles
    switches: dict[str, object] = {}
    keys = list(authored_switches)
    for i in range(ns):
        button, mode, on, off, name, _ = SWITCH.unpack_from(
            data, switch_start + i * SWITCH.size
        )
        key = keys[i] if i < len(keys) else name.split(b'\0')[0].decode('utf-8')
        original = authored_switches.get(key, {})
        value = {
            k: v
            for k, v in cast(dict[str, object], original).items()
            if k in ('name', 'style')
        }
        value['button'] = chr(ord('A') + button)
        if mode:
            value['mode'] = ('press', 'momentary', 'latched')[mode]
        value['on'] = _patch(data, offset, on, metadata, set())
        if off != 65535:
            value['off'] = _patch(data, offset, off, metadata, set())
        switches[key] = value
    result['switches'] = switches
    programs: list[dict[str, object]] = []
    for i in range(nodes):
        address = (
            offset + struct.unpack_from('<I', data, offset + GRAPH.size + i * 4)[0]
        )
        if data[address] != 1:
            continue
        entry = dict(
            cast(
                dict[str, object], cast(dict[str, object], metadata[str(i)])['_program']
            )
        )
        instrument = struct.unpack_from('<H', data, address + 4)[0]
        identity = meta + METADATA.size + instrument * 64
        entry.update(
            instrument=_text(data, identity, 32), setup=_text(data, identity + 32, 32)
        )
        if entry not in programs:
            programs.append(entry)
    result['definitions'] = {'setups': setups, 'programs': programs}
    if offset + size > len(data):
        raise ValueError('truncated Patch graph')
    return tomlkit.dumps(result)


def _patch(
    data: bytes,
    offset: int,
    index: int,
    metadata: dict[str, object],
    ancestors: set[int],
) -> dict[str, object]:
    _, section_size, count, _, _, _ = GRAPH.unpack_from(data, offset)
    if index >= count or index in ancestors:
        raise ValueError('invalid or cyclic Patch graph')
    address = (
        offset + struct.unpack_from('<I', data, offset + GRAPH.size + index * 4)[0]
    )
    kind, flags, size = struct.unpack_from('<BBH', data, address)
    cursor = address + 4
    if cursor + size > offset + section_size:
        raise ValueError('truncated Patch node')
    value: dict[str, object] = {
        k: v
        for k, v in cast(dict[str, object], metadata.get(str(index), {})).items()
        if k != '_program'
    }
    if kind == 1:
        instrument, program, msb, lsb, volume, pan, bank = struct.unpack_from(
            '<HBBBBBI', data, cursor
        )
        value.update(
            type='program-change',
            instrument=_text(data, 76 + instrument * 68, 32),
            program=program + 1,
        )
        origin = cast(
            dict[str, object], cast(dict[str, object], metadata[str(index)])['_program']
        )
        if origin['program'] != program + 1 or origin.get('bank') != (
            None if bank == 0xFFFFFFFF else bank
        ):
            value.pop('name', None)
            value.setdefault(
                'display_name', _original_label(data, offset, index, origin)
            )
        if bank != 0xFFFFFFFF:
            value['bank'] = bank
        if volume != 255:
            value['volume'] = volume
        if pan != 255:
            value['pan'] = pan - 64
    elif kind == 2:
        output = _text(data, cursor, 32)
        message = data[cursor + 32 : cursor + size]
        value.update(output=output)
        if flags == 9:
            value.update(type='sysex', data=list(message[1:-1]))
        elif flags == 1:
            value.update(type='midi', bytes=list(message))
        elif flags == 2:
            value.update(
                type='pitch-bend',
                channel=(message[0] & 15) + 1,
                value=(message[1] | message[2] << 7) - 8192,
            )
        elif flags == 4:
            value.update(
                type='nrpn',
                channel=(message[0] & 15) + 1,
                parameter=message[2] << 7 | message[5],
                value=message[8] << 7 | message[11],
            )
        else:
            value.update(
                type='control-change',
                channel=(message[0] & 15) + 1,
                controller=message[1],
                value=message[2],
            )
    elif kind == 3:
        operation, argument = struct.unpack_from('<BI', data, cursor)
        value.update(
            type='state',
            operation=next(k for k, v in STATE_OPERATIONS.items() if v == operation),
        )
        if operation == 1:
            value['value'] = argument
        if operation == 2:
            value['value'] = bool(argument)
    elif kind == 4:
        value.update(
            type='stock-firmware',
            exit_buttons='+'.join(
                c for i, c in enumerate('ABCDEF') if data[cursor] & (1 << i)
            ),
        )
    elif kind == 5:
        children = struct.unpack_from('<H', data, cursor)[0]
        value.update(
            type='sequence',
            patches=[
                _patch(
                    data,
                    offset,
                    struct.unpack_from('<H', data, cursor + 2 + i * 2)[0],
                    metadata,
                    ancestors | {index},
                )
                for i in range(children)
            ],
        )
    elif kind == 6:
        value.update(type='reference', patch=_text(data, cursor + 64, 64))
        if document := _text(data, cursor, 64):
            value['document'] = document
    else:
        raise ValueError('unknown Patch type')
    return value


def _edited_program(
    data: bytes,
    offset: int,
    index: int,
    metadata: dict[str, object],
    named: dict[str, int],
) -> bool:
    seen: set[int] = set()
    while index not in seen:
        seen.add(index)
        address = (
            offset + struct.unpack_from('<I', data, offset + GRAPH.size + index * 4)[0]
        )
        if data[address] == 1:
            origin = cast(
                dict[str, object],
                cast(dict[str, object], metadata[str(index)])['_program'],
            )
            bank = struct.unpack_from('<I', data, address + 11)[0]
            return origin['program'] != data[address + 6] + 1 or origin.get('bank') != (
                None if bank == 0xFFFFFFFF else bank
            )
        if data[address] != 6 or data[address + 4]:
            return False
        key = _text(data, address + 68, 64)
        if key not in named:
            return False
        index = named[key]
    return False


def _original_label(
    data: bytes, offset: int, index: int, origin: dict[str, object]
) -> str:
    if label := origin.get('display_name') or origin.get('name'):
        return str(label)
    _, _, nodes, switches, roots, named = GRAPH.unpack_from(data, offset)
    cursor = offset + GRAPH.size + nodes * 4 + roots * 2 + switches * SWITCH.size
    for _ in range(named):
        key, node = NAMED.unpack_from(data, cursor)
        cursor += NAMED.size
        if node == index:
            return key.split(b'\0')[0].decode('ascii')
    bank = origin.get('bank')
    return (
        f'PC {origin["program"]}'
        if bank is None
        else f'Bank {bank} / PC {origin["program"]}'
    )


def _text(data: bytes, offset: int, length: int) -> str:
    return data[offset : offset + length].split(b'\0')[0].decode('utf-8')
