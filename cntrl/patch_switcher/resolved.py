"""Export editable authoring from the authoritative compiled Patch graph."""

import struct
from typing import cast
from zlib import crc32

import tomlkit

from . import (
    arpeggiator,
    counters,
    display,
    graphics,
    inputs,
    layout,
    looper,
    midi_endpoints,
    midi_messages,
    midi_processing,
    page_settings,
    pickup,
    relay_outputs,
)
from .compiler import (
    BINDING_HOOKS_OFFSET,
    BINDING_RECORD_SIZE,
    BINDING_SHIFTED_OFFSET,
    DOCUMENT_VERSION,
    GESTURE_EVENTS,
    GESTURE_TIMINGS,
    GESTURES,
    GESTURES_FLAG,
    GRAPH,
    HEADER,
    LOOPER_ACTIONS,
    METADATA,
    NAMED,
    SCROLL_DIRECTIONS,
    SHIFTED_ACTIONS,
    STATE_OPERATIONS,
    SWITCH,
    SWITCH_HOOKS,
    TRS_MODES,
)
from .display import read_source
from .models import MidiEndpoint, Setup


def refresh_memory(data: bytes, fonts: dict[int, bytes], *, switches: bool) -> bytes:
    """Refresh current setup memory accounting directly from its wire records."""
    graph = graph_offset(data)
    if struct.unpack_from('<H', data, 4)[0] != DOCUMENT_VERSION:
        raise ValueError('upgrade setup before refreshing its memory estimate')
    _, _, _, _, instruments, cycles, patches, *_ = HEADER.unpack_from(data)
    metadata = 76 + instruments * 68 + cycles * 68 + patches * 82
    page = _page_offsets(data)[0]
    settings = Setup.model_validate(tomlkit.parse(export(data)).unwrap())
    windows = [
        data[i : i + graphics.SCREEN.size] for i in range(28, 76, graphics.SCREEN.size)
    ]
    display_start = graph + GRAPH.unpack_from(data, graph)[1]
    endpoints = endpoint_records(data)
    cursor = (
        _sections(data)[2]
        + inputs.HEADER.size
        + len(input_labels(data)) * inputs.RECORD.size
    )
    cursor = counters.section_end(data, relay_outputs.section_end(data, cursor))
    end = midi_processing.section_end(data, cursor)
    streams = data[cursor:end]
    cursor = pickup.section_end(data, end)
    end = looper.section_end(data, cursor)
    loop = data[cursor:end]
    cursor = end
    end = arpeggiator.section_end(data, cursor)
    arp = data[cursor:end]
    needed = (
        sum(p.ram2_bytes for p in settings.pages.values())
        + display.estimate_font_ram(windows, data[display_start:], fonts)
        + layout.estimate_ram(
            settings.pages,
            instruments,
            cycles,
            switches=switches,
            regions=data[page + 7],
        )
        + midi_processing.estimate_ram(streams, endpoint_count=len(endpoints))
        + looper.estimate_ram(loop)
        + arpeggiator.estimate_ram(arp)
    )
    address = metadata + METADATA.size - 8
    if struct.unpack_from('<I', data, address)[0] == needed:
        return data
    refreshed = bytearray(data)
    struct.pack_into('<I', refreshed, address, needed)
    struct.pack_into('<I', refreshed, 20, crc32(refreshed[28:]))
    return bytes(refreshed)


def prepare_restore(
    data: bytes,
    fonts: dict[int, bytes],
    *,
    switches: bool,
    preserve_memory_estimates: bool = False,
) -> bytes:
    """Prepare a backup for current firmware or retain same-format accounting."""
    upgraded = upgrade(data)
    if preserve_memory_estimates:
        if upgraded != data:
            raise ValueError(
                'preserving memory estimates requires current-format setups'
            )
        return data
    return refresh_memory(upgraded, fonts, switches=switches)


def upgrade(data: bytes) -> bytes:
    """Decode a supported setup and upgrade its format without recompilation.

    Preserve graph node IDs and all existing records. Extend only legacy page
    layout and gesture records, then update section offsets, lengths and CRC.
    """
    graph = graph_offset(data)
    version = struct.unpack_from('<H', data, 4)[0]
    if version == DOCUMENT_VERSION:
        export(data)
        return data
    page, gestures, shifted = _page_offsets(data)
    gesture_size = _gesture_size(version)
    shifted_size = gesture_size + 4
    edits = [(page, layout.section_end(data, page), layout.upgrade(data, page))]
    if gestures is not None:
        edits.append(
            (
                gestures,
                gestures + 5 * gesture_size,
                b''.join(
                    _extend_gestures(data[i : i + gesture_size])
                    for i in range(gestures, gestures + 5 * gesture_size, gesture_size)
                ),
            )
        )
    if shifted is not None:
        edits.append(
            (
                shifted,
                shifted + 5 * shifted_size,
                b''.join(
                    _extend_gestures(data[i : i + shifted_size], 4)
                    for i in range(shifted, shifted + 5 * shifted_size, shifted_size)
                ),
            )
        )
    size = GRAPH.unpack_from(data, graph)[1]
    edits.append((graph, graph + size, _upgrade_graph(data, graph, gesture_size)))
    converted = bytearray(data)
    for start, end, replacement in sorted(edits, reverse=True):
        converted[start:end] = replacement
    struct.pack_into('<H', converted, 4, DOCUMENT_VERSION)
    struct.pack_into('<I', converted, 8, len(converted))
    struct.pack_into('<I', converted, 20, crc32(converted[28:]))
    result = bytes(converted)
    export(result)
    return result


def _page_offsets(data: bytes) -> tuple[int, int | None, int | None]:
    if len(data) < 76:
        raise ValueError('truncated setup')
    magic, version, _, length, ni, nc, np, flags, crc, _ = HEADER.unpack_from(data)
    if (
        magic != b'CSW1'
        or version not in (36, 38, 39, 40, DOCUMENT_VERSION)
        or flags
        & ~(
            0x1FFF
            | GESTURES_FLAG
            | page_settings.EXPRESSION_FLAG
            | page_settings.CLOCK_FLAG
        )
        or length != len(data)
        or crc32(data[28:]) != crc
    ):
        raise ValueError('invalid setup header/checksum')
    meta = 76 + ni * 68 + nc * 68 + np * 82
    if data[meta : meta + 4] != b'CMTA':
        raise ValueError('missing setup metadata')
    nb = struct.unpack_from('<H', data, meta + 4)[0]
    cursor = meta + METADATA.size + ni * 64 + nb * 8 + np * 4
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
    gestures = None
    shifted = None
    if flags & GESTURES_FLAG:
        gestures = cursor
        cursor += 5 * _gesture_size(version)
    if data[cursor : cursor + 4] == b'CHOK':
        cursor += 4 + 5 * SWITCH_HOOKS.size
    if data[cursor : cursor + 4] == b'CSHF':
        shifted = cursor + 4
        cursor += 4 + 5 * (_gesture_size(version) + 4)
    if cursor + 8 > len(data) or data[cursor + 6] != (1 if version < 39 else 2):
        raise ValueError('invalid page layout version')
    return cursor, gestures, shifted


def _sections(data: bytes) -> tuple[int, int, int]:
    """Locate sections in the original bytes, including supported old formats."""
    cursor = _page_offsets(data)[0]
    np = HEADER.unpack_from(data)[6]
    cursor = layout.section_end(data, cursor)
    if cursor + 8 > len(data) or data[cursor : cursor + 4] != b'CMOV':
        raise ValueError('missing movie metadata')
    startup, idle = struct.unpack_from('<HH', data, cursor + 4)
    cursor += 8 + 4 * (startup + idle)
    endpoint_offset = cursor
    records = midi_endpoints.unpack(data, cursor)
    cursor += 6 + len(records) * midi_endpoints.RECORD.size
    input_offset = cursor
    labels = inputs.unpack(data, cursor)
    cursor += inputs.HEADER.size + len(labels) * inputs.RECORD.size
    cursor = relay_outputs.section_end(data, cursor)
    cursor = counters.section_end(data, cursor)
    cursor = midi_processing.section_end(data, cursor)
    cursor = pickup.section_end(data, cursor)
    cursor = looper.section_end(data, cursor)
    cursor = arpeggiator.section_end(data, cursor)
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
    return cursor, endpoint_offset, input_offset


def _gesture_size(version: int) -> int:
    return 20 if version < 40 else 24 if version == 40 else GESTURES.size


def _extend_gestures(record: bytes, prefix: int = 0) -> bytes:
    if len(record) not in (prefix + 20, prefix + 24):
        raise ValueError('truncated legacy gestures')
    events = (len(record) - prefix - 8) // 2
    return record[:-8] + b'\xff\xff' * (len(GESTURE_EVENTS) - events) + record[-8:]


def _upgrade_graph(data: bytes, offset: int, gesture_size: int) -> bytes:
    magic, size, nodes, switches, roots, named = GRAPH.unpack_from(data, offset)
    prefix = (
        GRAPH.size + nodes * 4 + roots * 2 + switches * SWITCH.size + named * NAMED.size
    )
    cursor = prefix
    result = bytearray(data[offset : offset + prefix])
    if magic != b'CPAT' or prefix > size or offset + size > len(data):
        raise ValueError('invalid legacy Patch graph')
    for index in range(nodes):
        if (
            struct.unpack_from('<I', data, offset + GRAPH.size + index * 4)[0] != cursor
            or cursor + 4 > size
        ):
            raise ValueError('invalid legacy Patch node offset')
        kind, flags, length = struct.unpack_from('<BBH', data, offset + cursor)
        payload = data[offset + cursor + 4 : offset + cursor + 4 + length]
        if cursor + 4 + length > size:
            raise ValueError('truncated legacy Patch node')
        cursor += 4 + length
        if kind == 10:
            hooks = SWITCH.size + gesture_size
            shifted = hooks + SWITCH_HOOKS.size
            record_size = shifted + gesture_size + 4
            if length < 220 or length != 220 + payload[66] * record_size:
                raise ValueError('invalid legacy control bindings')
            records = []
            for position in range(220, length, record_size):
                record = payload[position : position + record_size]
                records.append(
                    record[: SWITCH.size]
                    + _extend_gestures(record[SWITCH.size : hooks])
                    + record[hooks:shifted]
                    + _extend_gestures(record[shifted:], 4)
                )
            payload = payload[:220] + b''.join(records)
        struct.pack_into('<I', result, GRAPH.size + index * 4, len(result))
        result.extend(struct.pack('<BBH', kind, flags, len(payload)) + payload)
    if cursor != size:
        raise ValueError('invalid legacy Patch graph length')
    struct.pack_into('<I', result, 4, len(result))
    return bytes(result)


def graph_offset(data: bytes) -> int:
    """Locate the graph following variable name metadata."""
    return _sections(data)[0]


def endpoint_records(data: bytes) -> list[MidiEndpoint]:
    return midi_endpoints.unpack(data, _sections(data)[1])


def input_labels(data: bytes) -> list[str]:
    return inputs.unpack(data, _sections(data)[2])


def arm_memory(data: bytes) -> dict[str, int]:
    """Additional arm admission estimates, excluding retained active/undo takes.

    Physical inputs have one cable. USB inputs conservatively allow sixteen
    cables because the setup does not encode the connected USB descriptor.
    """
    input_offset = _sections(data)[2]
    cursor = (
        input_offset + inputs.HEADER.size + len(input_labels(data)) * inputs.RECORD.size
    )
    cursor = counters.section_end(data, relay_outputs.section_end(data, cursor))
    cursor = pickup.section_end(data, midi_processing.section_end(data, cursor))
    result: dict[str, int] = {}
    endpoints = endpoint_records(data)
    for name, section, estimate in (
        ('looper', looper, looper.estimate_arm_ram),
        ('arpeggiator', arpeggiator, arpeggiator.estimate_arm_ram),
    ):
        end = section.section_end(data, cursor)
        if end != cursor:
            settings = data[cursor:end]
            values = section.CONFIG.unpack(settings)
            channels, selected_inputs = values[2:4] if name == 'looper' else values[5:7]
            contexts = channels.bit_count() * sum(
                1 if e.transport == 'physical' else 16
                for e in endpoints
                if e.input and selected_inputs & (1 << e.route)
            )
            result[name] = estimate(settings, capture_contexts=contexts)
        cursor = end
    return result


def patch(data: bytes, node: int) -> dict[str, object]:
    """Decode any graph node into the common tagged Patch authoring table."""
    if len(data) >= 6 and struct.unpack_from('<H', data, 4)[0] != DOCUMENT_VERSION:
        data = upgrade(data)
    offset = graph_offset(data)
    source = read_source(data, offset)
    labels = input_labels(data)
    metadata = cast(dict[str, object], source.get('patch_metadata', {}))
    return _patch(data, offset, node, metadata, set(), labels)


def export(data: bytes) -> str:
    if len(data) >= 6 and struct.unpack_from('<H', data, 4)[0] != DOCUMENT_VERSION:
        data = upgrade(data)
    offset = graph_offset(data)
    endpoints = endpoint_records(data)
    names = {e.route: e.name for e in endpoints}
    jacks = midi_endpoints.ports(endpoints)
    _, _, hold, _, ni, nc, np, flags, _, timeout = HEADER.unpack_from(data)
    meta = 76 + ni * 68 + nc * 68 + np * 82
    nb = struct.unpack_from('<H', data, meta + 4)[0]
    source = read_source(data, offset)
    labels = input_labels(data)
    metadata = cast(dict[str, object], source.pop('patch_metadata', {}))
    source.pop('program_styles', None)
    source.pop('program_tags', None)
    source.pop('patch_styles', None)
    source.pop('patch_tags', None)
    authored_switches = cast(dict[str, object], source.pop('switches', {}))
    result: dict[str, object] = {
        'brightness': flags & 255,
        'animation_color_cycle': bool(flags & 1024),
        'is_midi_interface': bool(flags & 512),
        'usb_device_midi': bool(flags & 2048),
        'usb_host_midi': bool(flags & 4096),
        'fast_switches': bool(data[meta + METADATA.size - 9] & 1),
        'forward_device_to_host': not bool(data[meta + METADATA.size - 9] & 2),
        'screensaver_timeout': timeout // 1000,
    }
    scrolling = struct.unpack_from('<H3B', data, meta + METADATA.size - 14)
    result['scrolling'] = {
        'horizontal_duration': struct.unpack_from('<H', data, meta + 6)[0] / 1000,
        'vertical_duration': scrolling[0] / 1000,
        **{
            k: SCROLL_DIRECTIONS[d]
            for k, d in zip(('send', 'patches', 'cycles'), scrolling[1:], strict=True)
        },
    }
    period, minimum, maximum = struct.unpack_from('<HBB', data, meta + 8)
    result['target_pulse'] = {
        'period': period / 1000,
        'delay': struct.unpack_from('<H', data, meta + 14)[0] / 1000,
        'minimum': minimum,
        'maximum': maximum,
    }
    repeat = struct.unpack_from('<H', data, meta + 12)[0]
    result['navigation'] = {'hold_delay': hold / 1000, 'repeat_interval': repeat / 1000}
    pages = page_settings.unpack(data, meta + 16)
    cursor = meta + METADATA.size + ni * 64 + nb * 8 + np * 4
    if flags & page_settings.EXPRESSION_FLAG:
        pages['expression'] = page_settings.unpack_expression(data, cursor, jacks)
        cursor += page_settings.EXPRESSION.size * 4
    if flags & page_settings.CLOCK_FLAG:
        pages.update(page_settings.unpack_clock(data, cursor, names))
        cursor += page_settings.CLOCK.size
    if data[cursor : cursor + 4] == b'CKBD':
        pages['keyboard'] = page_settings.unpack_keyboard(data, cursor + 4)
        cursor += (
            6
            + struct.unpack_from('<H', data, cursor + 4)[0]
            * page_settings.KEYBOARD.size
        )
    result['page_settings'] = pages
    if flags & GESTURES_FLAG:
        cursor += 5 * GESTURES.size
    if data[cursor : cursor + 4] == b'CHOK':
        cursor += 4 + 5 * SWITCH_HOOKS.size
    if data[cursor : cursor + 4] == b'CSHF':
        cursor += 4 + 5 * SHIFTED_ACTIONS.size
    result.update(layout.unpack(data, cursor))
    # Retained source metadata predates the wire upgrade. Reuse its concise
    # page authoring only when compiling it describes these exact page bytes.
    # Other source metadata remains untouched, including node IDs and assets.
    if 'pages' in source:
        windows = [
            _text(data, cursor + 12 + i * 32, 32) for i in range(data[cursor + 5])
        ]
        try:
            authored = Setup.model_validate(
                {k: source[k] for k in ('pages', 'regions', 'layout') if k in source}
            )
            same_pages = (
                layout.pack(authored, windows)
                == data[cursor : layout.section_end(data, cursor)]
            )
        except ValueError:
            same_pages = False
        if not same_pages:
            # Geometry comes from the upgraded wire, but explicit per-page
            # memory allowances exist only in retained authoring metadata.
            authored_pages = source.get('pages')
            actual_pages = cast(dict[str, dict[str, object]], result['pages'])
            if isinstance(authored_pages, dict):
                for name, raw in authored_pages.items():
                    if (
                        name in actual_pages
                        and isinstance(raw, dict)
                        and 'ram2_bytes' in raw
                    ):
                        actual_pages[name]['ram2_bytes'] = raw['ram2_bytes']
            for key in ('pages', 'regions', 'layout'):
                source.pop(key, None)
    modes = data[meta + METADATA.size - 18 : meta + METADATA.size - 14]
    if any(m >= len(TRS_MODES) for m in modes):
        raise ValueError('invalid TRS port role')
    if any(modes):
        result['trs_ports'] = {
            j: TRS_MODES[m] for j, m in zip(jacks, modes, strict=True) if m
        }
    combined = {
        j: 'combined'
        for i, j in enumerate(jacks)
        if data[meta + METADATA.size - 9] & (4 << i)
    }
    if combined:
        result['auxiliary_switches'] = combined
    result.update(source)
    input_offset = _sections(data)[2]
    relay_offset = input_offset + inputs.HEADER.size + len(labels) * inputs.RECORD.size
    if relays := relay_outputs.unpack(data, relay_offset):
        result['relays'] = relays
    counter_offset = relay_outputs.section_end(data, relay_offset)
    looper_offset = pickup.section_end(
        data,
        midi_processing.section_end(data, counters.section_end(data, counter_offset)),
    )
    if settings := looper.unpack(data, looper_offset, names):
        pages['looper'] = settings
    if settings := arpeggiator.unpack(
        data, looper.section_end(data, looper_offset), names
    ):
        pages['arpeggiator'] = settings
    if definitions := counters.unpack(data, counter_offset):
        result['counters'] = {
            k: v.model_dump(exclude_none=True) for k, v in definitions.items()
        }
    instruments: dict[str, object] = {}
    profiles: list[dict[str, object]] = []
    for i in range(ni):
        address = 76 + i * 68
        identity = meta + METADATA.size + i * 64
        instrument, profile = _text(data, identity, 32), _text(data, identity + 32, 32)
        instruments[_text(data, address, 32)] = {
            'instrument': instrument,
            'profile': profile,
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
            'name': profile,
            'banks': banks,
        }
        if value not in profiles:
            profiles.append(value)
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
        identity = key.split(b'\0')[0].decode('utf-8')
        named_nodes[identity] = index
        named[identity] = _patch(data, offset, index, metadata, set(), labels)
    result['patches'] = named
    patch_cycles: dict[str, object] = {}
    for i in range(nc):
        address = 76 + ni * 68 + i * 68
        start, count = struct.unpack_from('<HH', data, address + 64)
        values: list[dict[str, object]] = []
        for position in range(start, start + count):
            root = roots[position]
            value = _patch(data, offset, root, metadata, set(), labels)
            if 'patch' in value and _edited_program(
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
        value['button'] = labels[button]
        if mode:
            value['mode'] = ('press', 'momentary', 'latched')[mode]
        value['on'] = _patch(data, offset, on, metadata, set(), labels)
        hooks = meta + METADATA.size + ni * 64 + nb * 8 + np * 4
        if flags & page_settings.EXPRESSION_FLAG:
            hooks += page_settings.EXPRESSION.size * 4
        if flags & page_settings.CLOCK_FLAG:
            hooks += page_settings.CLOCK.size
        if data[hooks : hooks + 4] == b'CKBD':
            hooks += (
                6
                + struct.unpack_from('<H', data, hooks + 4)[0]
                * page_settings.KEYBOARD.size
            )
        if flags & GESTURES_FLAG:
            hooks += 5 * GESTURES.size
        if data[hooks : hooks + 4] == b'CHOK':
            enter, exit, override = SWITCH_HOOKS.unpack_from(
                data, hooks + 4 + i * SWITCH_HOOKS.size
            )
            if enter != 65535:
                value['enter'] = {
                    'action': _patch(data, offset, enter, metadata, set(), labels),
                    'override': override,
                }
            if exit != 65535:
                value['exit'] = _patch(data, offset, exit, metadata, set(), labels)
            hooks += 4 + 5 * SWITCH_HOOKS.size
        if data[hooks : hooks + 4] == b'CSHF':
            fields = SHIFTED_ACTIONS.unpack_from(
                data, hooks + 4 + i * SHIFTED_ACTIONS.size
            )
            if fields[0] != 65535:
                shifted: dict[str, object] = {
                    'on': _patch(data, offset, fields[0], metadata, set(), labels)
                }
                if fields[1] != 65535:
                    shifted['off'] = _patch(
                        data, offset, fields[1], metadata, set(), labels
                    )
                if fields[2 + len(GESTURE_EVENTS)]:
                    shifted['gestures'] = {
                        t: fields[2 + len(GESTURE_EVENTS) + j] / 1000
                        for j, t in enumerate(GESTURE_TIMINGS)
                    } | {
                        e: _patch(data, offset, fields[2 + j], metadata, set(), labels)
                        for j, e in enumerate(GESTURE_EVENTS)
                        if fields[2 + j] != 65535
                    }
                value['shifted'] = shifted
        if flags & GESTURES_FLAG:
            address = meta + METADATA.size + ni * 64 + nb * 8 + np * 4
            if flags & page_settings.EXPRESSION_FLAG:
                address += page_settings.EXPRESSION.size * 4
            if flags & page_settings.CLOCK_FLAG:
                address += page_settings.CLOCK.size
            if data[address : address + 4] == b'CKBD':
                address += (
                    6
                    + struct.unpack_from('<H', data, address + 4)[0]
                    * page_settings.KEYBOARD.size
                )
            fields = GESTURES.unpack_from(data, address + i * GESTURES.size)
            if fields[len(GESTURE_EVENTS)]:
                gesture: dict[str, object] = {
                    t: fields[len(GESTURE_EVENTS) + j] / 1000
                    for j, t in enumerate(GESTURE_TIMINGS)
                }
                for j, event in enumerate(GESTURE_EVENTS):
                    if fields[j] != 65535:
                        gesture[event] = _patch(
                            data, offset, fields[j], metadata, set(), labels
                        )
                value['gestures'] = gesture
        if off != 65535:
            value['off'] = _patch(data, offset, off, metadata, set(), labels)
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
            instrument=_text(data, identity, 32), profile=_text(data, identity + 32, 32)
        )
        if entry not in programs:
            programs.append(entry)
    result['definitions'] = {'profiles': profiles, 'programs': programs}
    if offset + size > len(data):
        raise ValueError('truncated Patch graph')
    return tomlkit.dumps(result)


def _patch(
    data: bytes,
    offset: int,
    index: int,
    metadata: dict[str, object],
    ancestors: set[int],
    labels: list[str],
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
    properties = cast(dict[str, object], metadata.get(str(index), {}))
    if isinstance(authored := properties.get('_derived'), dict):
        program_cursor, program_kind = cursor, kind
        program_seen = {index}
        while program_kind in (10, 12):
            child = struct.unpack_from('<H', data, program_cursor)[0]
            if child >= count or child in program_seen:
                raise ValueError('invalid or cyclic Patch graph')
            program_seen.add(child)
            child_address = (
                offset
                + struct.unpack_from('<I', data, offset + GRAPH.size + child * 4)[0]
            )
            program_cursor, program_kind = child_address + 4, data[child_address]
        if program_kind != 1:
            return dict(authored)
        original = bytes.fromhex(str(properties['_derived_program']))
        current = data[program_cursor : program_cursor + len(original)]
        if original == current:
            return dict(authored)
        # Volume and pan edits are expressible as ordinary overrides.
        if original[:5] == current[:5] and original[7:] == current[7:]:
            variation = dict(authored)
            for field, position, bias in (('volume', 5, 0), ('pan', 6, 64)):
                if current[position] != original[position]:
                    if current[position] == 255:
                        break
                    variation[field] = current[position] - bias
            else:
                return variation
        # Export the actual program when an edit changes its catalogue identity
        # or clears an inherited setting (TOML has no null override).
    value = {k: v for k, v in properties.items() if not k.startswith('_')}
    if kind == 14:
        counter, backward = struct.unpack_from('<HB', data, cursor)
        input_offset = _sections(data)[2]
        relay_offset = (
            input_offset + inputs.HEADER.size + len(labels) * inputs.RECORD.size
        )
        names = list(
            counters.unpack(data, relay_outputs.section_end(data, relay_offset))
        )
        value.update(
            operation='scroll-counter',
            counter=names[counter],
            direction='backward' if backward else 'forward',
        )
    elif kind == 1:
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
        if flags == 4:
            value.update(
                type='nrpn',
                channel=(message[0] & 15) + 1,
                parameter=message[2] << 7 | message[5],
                value=message[8] << 7 | message[11],
            )
        elif flags == 10:
            value.update(midi_messages.decode_parameter(message))
        else:
            value.update(midi_messages.decode(message))
    elif kind == 3:
        operation, argument = struct.unpack_from('<BI', data, cursor)
        value['operation'] = next(
            k for k, v in STATE_OPERATIONS.items() if v == operation
        )
        if operation == 1:
            value['value'] = argument
        if operation == 2:
            value['value'] = bool(argument)
    elif kind == 4:
        value.update(
            operation='stock-firmware',
            exit_buttons='+'.join(
                c for i, c in enumerate(labels) if data[cursor] & (1 << i)
            ),
        )
    elif kind == 5:
        children = struct.unpack_from('<H', data, cursor)[0]
        value['patches'] = [
            _patch(
                data,
                offset,
                struct.unpack_from('<H', data, cursor + 2 + i * 2)[0],
                metadata,
                ancestors | {index},
                labels,
            )
            for i in range(children)
        ]
    elif kind == 8:
        filename, outputs, loops, pre, post, speed, reverse, transpose = (
            struct.unpack_from('<96sHIIIdBb', data, cursor)
        )
        value.update(
            operation='play-midi-file',
            filename=filename.split(b'\0')[0].decode('ascii'),
            outputs=[
                e.name for e in endpoint_records(data) if outputs & (1 << e.route)
            ],
            loops=loops,
            pre_delay=pre,
            post_delay=post,
            speed=speed,
            reverse=bool(reverse),
            transpose=transpose,
        )
    elif kind == 15:
        action, filename = struct.unpack_from('<B96s', data, cursor)
        value.update(operation='midi-looper', action=LOOPER_ACTIONS[action])
        if filename[0]:
            value['filename'] = filename.split(b'\0')[0].decode('ascii')
    elif kind == 9:
        channels, input_mask = struct.unpack_from('<HH', data, cursor)
        value.update(
            operation='record-midi-file',
            channels=[i + 1 for i in range(16) if channels & (1 << i)],
            inputs=[
                e.name for e in endpoint_records(data) if input_mask & (1 << e.route)
            ],
        )
    elif kind == 10:
        entry = _patch(
            data,
            offset,
            struct.unpack_from('<H', data, cursor)[0],
            metadata,
            ancestors | {index},
            labels,
        )
        bindings = page_settings.unpack_expression(
            data, cursor + 68, midi_endpoints.ports(endpoint_records(data))
        )
        original = cast(dict[str, dict[str, object]], properties.get('bindings', {}))
        for slot in range(data[cursor + 66]):
            record = cursor + 220 + slot * BINDING_RECORD_SIZE
            button, mode, on, off, _, _ = SWITCH.unpack_from(data, record)
            key = labels[button]

            def patch(node: int) -> dict[str, object]:
                return _patch(data, offset, node, metadata, ancestors | {index}, labels)

            def gestures(address: int) -> dict[str, object]:
                fields = GESTURES.unpack_from(data, address)
                return {
                    t: fields[len(GESTURE_EVENTS) + j] / 1000
                    for j, t in enumerate(GESTURE_TIMINGS)
                } | {
                    e: patch(fields[j])
                    for j, e in enumerate(GESTURE_EVENTS)
                    if fields[j] != 65535
                }

            authored = original.get(key, {})
            action: dict[str, object] = (
                {k: v for k, v in authored.items() if k in ('name', 'style')}
                if isinstance(authored, dict)
                else {}
            )
            action['on'] = patch(on)
            selected = action['on']
            label = (
                selected.get('display_name')
                or selected.get('name')
                or selected.get('patch')
                or key
            )
            if 'name' not in action and _text(data, record + 6, 64) != label:
                action['name'] = _text(data, record + 6, 64)
            if mode:
                action.update(
                    mode=('press', 'momentary', 'latched')[mode], off=patch(off)
                )
            if struct.unpack_from(
                '<H', data, record + SWITCH.size + len(GESTURE_EVENTS) * 2
            )[0]:
                action['gestures'] = gestures(record + 72)
            enter, exit, override = SWITCH_HOOKS.unpack_from(
                data, record + BINDING_HOOKS_OFFSET
            )
            if enter != 65535:
                action['enter'] = {'action': patch(enter), 'override': override}
            if exit != 65535:
                action['exit'] = patch(exit)
            shifted_on, shifted_off = struct.unpack_from(
                '<HH', data, record + BINDING_SHIFTED_OFFSET
            )
            if shifted_on != 65535:
                shifted: dict[str, object] = {'on': patch(shifted_on)}
                if shifted_off != 65535:
                    shifted['off'] = patch(shifted_off)
                if struct.unpack_from(
                    '<H',
                    data,
                    record + BINDING_SHIFTED_OFFSET + 4 + len(GESTURE_EVENTS) * 2,
                )[0]:
                    shifted['gestures'] = gestures(record + BINDING_SHIFTED_OFFSET + 4)
                action['shifted'] = shifted
            bindings[key] = (
                action['on']
                if set(action) == {'on'} and 'on' not in authored
                else action
            )
        return (
            entry
            | value
            | {
                'bindings': {k: bindings[k] for k in original if k in bindings}
                | bindings
            }
        )
    elif kind == 11:
        contact, action, duration = struct.unpack_from('<32sBI', data, cursor)
        value.update(
            operation='relay',
            relay=contact.split(b'\0')[0].decode('ascii'),
            action=relay_outputs.ACTIONS[action],
        )
        if duration:
            value['duration'] = duration / 1000
    elif kind == 12:
        entry = _patch(
            data,
            offset,
            struct.unpack_from('<H', data, cursor)[0],
            metadata,
            ancestors | {index},
            labels,
        )
        return entry | value
    elif kind == 13:
        stream, enabled = struct.unpack_from('<72sB', data, cursor)
        value.update(
            operation='midi-stream',
            stream=stream.split(b'\0')[0].decode('ascii'),
            enabled=bool(enabled),
        )
    elif kind == 7:
        value.update(duration=struct.unpack_from('<I', data, cursor)[0] / 1000)
    elif kind == 6:
        value['patch'] = _text(data, cursor + 2, 64)
        if document := properties.get('_document'):
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
        if data[address] in (10, 12):
            index = struct.unpack_from('<H', data, address + 4)[0]
            continue
        if (
            data[address] != 6
            or struct.unpack_from('<H', data, address + 4)[0] != 65535
        ):
            return False
        key = _text(data, address + 6, 64)
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
            return key.split(b'\0')[0].decode('utf-8')
    bank = origin.get('bank')
    return (
        f'PC {origin["program"]}'
        if bank is None
        else f'Bank {bank} / PC {origin["program"]}'
    )


def _text(data: bytes, offset: int, length: int) -> str:
    return data[offset : offset + length].split(b'\0')[0].decode('utf-8')
