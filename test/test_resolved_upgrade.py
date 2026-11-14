"""Historical setup upgrades preserve their authoritative compiled payload."""

import struct
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, layout, models, resolved


@pytest.fixture
def current_setup() -> bytes:
    return bytes.fromhex((ROOT / 'show.golden').read_text())


@pytest.mark.parametrize('version', [36, 38, 39, 40, 41, 42])
def test_historical_setup_preserves_graph_assets_and_page_order(version: int) -> None:
    # Actual outputs from a6916c8^, 1c8dd6b, c502794, 48fafdf, 32bdbab and 109a9cb.
    original = bytes.fromhex((ROOT / f'setup{version}.golden').read_text())
    assert struct.unpack_from('<H', original, 4)[0] == version
    upgraded = resolved.upgrade(original)
    assert resolved.export(original) == resolved.export(upgraded)
    assert struct.unpack_from('<H', upgraded, 4)[0] == compiler.DOCUMENT_VERSION
    assert struct.unpack_from('<I', upgraded, 8)[0] == len(upgraded)
    assert struct.unpack_from('<I', upgraded, 20)[0] == crc32(upgraded[28:])
    old_page, new_page = original.index(b'CPAG'), upgraded.index(b'CPAG')
    old_end, new_end = (
        layout.section_end(original, old_page),
        layout.section_end(upgraded, new_page),
    )
    assert upgraded[28:new_page] == original[28:old_page]
    assert upgraded[new_end:] == original[old_end:]
    old_layout = layout.unpack(original, old_page)
    old_pages = old_layout['pages']
    assert isinstance(old_pages, dict)
    source = tomlkit.parse(resolved.export(upgraded)).unwrap()
    assert list(source['pages']) == list(old_pages)
    assert source['regions'] == old_layout['regions']
    settings = models.Setup.model_validate(source)
    windows = [
        upgraded[new_page + 12 + i * 32 : new_page + 12 + (i + 1) * 32]
        .split(b'\0')[0]
        .decode('ascii')
        for i in range(upgraded[new_page + 5])
    ]
    assert layout.pack(settings, windows) == upgraded[new_page:new_end]
    assert original[resolved.graph_offset(original) :][:4] == b'CPAT'


def test_legacy_single_switcher_keeps_its_tool_cycle_and_full_width_position() -> None:
    original = bytes.fromhex((ROOT / 'setup36.golden').read_text())
    source = tomlkit.parse(resolved.export(resolved.upgrade(original))).unwrap()
    tools = [
        'status',
        'firmware',
        'cable-test',
        'panic',
        'trs-ports',
        'expression',
        'clock-transport',
        'clock-tempo',
        'keyboard',
        'tap-tempo',
        'full-performance',
    ]
    assert source['regions'] == {
        'performance': {'pages': ['patch-switcher']},
        'tools': {'pages': tools},
    }
    assert source['layout'] == {
        'order': ['performance', 'tools'],
        'left_wins': False,
        'focus': 'tools',
    }
    assert list(source['pages']) == tools[:-1] + ['patch-switcher', 'full-performance']
    assert source['pages']['patch-switcher']['min_windows'] == 1
    assert source['pages']['patch-switcher']['give_up_windows'] == [
        'right',
        'center',
        'left',
    ]
    assert source['pages']['full-performance']['type'] == 'None'
    assert source['pages']['full-performance']['min_windows'] == 0


def test_legacy_full_width_position_and_page_names_are_preserved() -> None:
    original = bytes.fromhex((ROOT / 'setup36.golden').read_text())
    offset = original.index(b'CPAG')
    section = bytearray(original[offset : layout.section_end(original, offset)])
    count, windows = section[4:6]
    start, size = 8 + windows * 32, 34 + windows
    records = [
        bytearray(section[start + i * size : start + (i + 1) * size])
        for i in range(count)
    ]
    records[0][:32] = layout.encoded_name('full-performance')
    # A legacy setup may start at its full-width switcher position rather than Status.
    section[start:] = b''.join(records[-1:] + records[:-1])
    source = layout.unpack(layout.upgrade(bytes(section), 0), 0)
    settings = models.Setup.model_validate(source)
    assert settings.regions['tools'].pages[:2] == [
        'full-performance-2',
        'full-performance',
    ]
    assert settings.pages['full-performance'].type == 'status'
    assert settings.pages['full-performance-2'].type == 'None'
    assert settings.regions['performance'].pages == ['patch-switcher']


def test_current_setup_is_returned_unchanged(current_setup: bytes) -> None:
    assert resolved.upgrade(current_setup) == current_setup


@pytest.mark.parametrize(
    'source',
    [
        {},
        {'pages': {'live': {'ram2_bytes': 123}, 'status': {'type': 'status'}}},
        {
            'midi': {
                'echo': {
                    'from': 'din',
                    'to': 'din',
                    'via': [{'delay': {'time': '0.1 s', 'repeats': 3}}],
                }
            }
        },
        {'pages': {'loop': {'type': 'midi-looper'}}},
        {'pages': {'arp': {'type': 'arpeggiator'}}},
    ],
)
def test_memory_refresh_matches_compiler_and_changes_only_estimate_and_crc(
    source: dict[str, object],
) -> None:
    target = models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))
    compiled = compiler.compile_show(source, {}, [], target)
    instruments, cycles, patches = compiler.HEADER.unpack_from(compiled)[4:7]
    address = (
        76 + instruments * 68 + cycles * 68 + patches * 82 + compiler.METADATA.size - 8
    )
    stale = bytearray(compiled)
    struct.pack_into('<I', stale, address, 0)
    struct.pack_into('<I', stale, 20, crc32(stale[28:]))
    refreshed = resolved.refresh_memory(
        bytes(stale), {}, switches=bool(target.max_switches)
    )
    assert refreshed == compiled
    assert (
        resolved.refresh_memory(refreshed, {}, switches=bool(target.max_switches))
        is refreshed
    )


def test_historical_memory_estimate_uses_migrated_layout() -> None:
    target = models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))
    old = bytes.fromhex((ROOT / 'setup36.golden').read_text())
    upgraded = resolved.upgrade(old)
    current_source = tomlkit.parse(resolved.export(upgraded)).unwrap()
    rebuilt = compiler.compile_show(current_source, {}, [], target)
    ni, nc, np = compiler.HEADER.unpack_from(upgraded)[4:7]
    address = 76 + ni * 68 + nc * 68 + np * 82 + compiler.METADATA.size - 8
    assert struct.unpack_from('<I', old, address)[0] == 0
    refreshed = resolved.refresh_memory(
        upgraded, {}, switches=bool(target.max_switches)
    )
    assert (
        struct.unpack_from('<I', refreshed, address)[0]
        == struct.unpack_from('<I', rebuilt, address)[0]
        > 0
    )
    assert refreshed[28:address] == upgraded[28:address]
    assert refreshed[address + 4 :] == upgraded[address + 4 :]
    assert refreshed[:20] == upgraded[:20] and refreshed[24:28] == upgraded[24:28]


def test_legacy_page_memory_allowance_survives_layout_source_normalization() -> None:
    old = bytes.fromhex((ROOT / 'setup36.golden').read_text())
    annotated = bytearray(old)
    position = old.index(b'ram2_bytes = 0') + len(b'ram2_bytes = ')
    annotated[position] = ord('7')
    struct.pack_into('<I', annotated, 20, crc32(annotated[28:]))
    upgraded = resolved.upgrade(bytes(annotated))
    source = models.Setup.model_validate(
        tomlkit.parse(resolved.export(upgraded)).unwrap()
    )
    assert sum(p.ram2_bytes for p in source.pages.values()) == 7
    baseline = resolved.refresh_memory(resolved.upgrade(old), {}, switches=True)
    refreshed = resolved.refresh_memory(upgraded, {}, switches=True)
    ni, nc, np = compiler.HEADER.unpack_from(upgraded)[4:7]
    address = 76 + ni * 68 + nc * 68 + np * 82 + compiler.METADATA.size - 8
    assert (
        struct.unpack_from('<I', refreshed, address)[0]
        == struct.unpack_from('<I', baseline, address)[0] + 7
    )


@pytest.mark.parametrize('version', [39, 40])
def test_legacy_gestures_shifted_actions_and_bound_nodes_keep_their_references(
    version: int,
) -> None:
    original = bytes.fromhex((ROOT / f'setup{version}.golden').read_text())
    page = original.index(b'CPAG')
    graph = resolved.graph_offset(original)
    magic, size, count, switches, roots, named = compiler.GRAPH.unpack_from(
        original, graph
    )
    flags = struct.unpack_from('<H', original, 18)[0]
    assert not flags & compiler.GESTURES_FLAG
    events = 6 if version < 40 else 8
    gesture = struct.pack(f'<{events + 4}H', *([0] * events), 20, 500, 300, 100)
    shifted = struct.pack(f'<{events + 6}H', *([0] * (events + 2)), 20, 500, 300, 100)
    record = compiler.SWITCH.pack(0, 1, 0, 0, b'Legacy action', 0)
    record += gesture + compiler.SWITCH_HOOKS.pack(0, 0, False) + shifted
    assert len(record) == (121 if version < 40 else 129)
    payload = bytearray(220)
    payload[2:66] = b'Legacy bindings'.ljust(64, b'\0')
    payload[66] = 1
    binding = struct.pack('<BBH', 10, 0, 220 + len(record)) + payload + record
    offsets = struct.unpack_from(f'<{count}I', original, graph + compiler.GRAPH.size)
    expanded = compiler.GRAPH.pack(
        magic, size + 4 + len(binding), count + 1, switches, roots, named
    )
    expanded += struct.pack(f'<{count + 1}I', *(i + 4 for i in offsets), size + 4)
    expanded += (
        original[graph + compiler.GRAPH.size + count * 4 : graph + size] + binding
    )
    legacy = bytearray(
        original[:page]
        + gesture * 5
        + b'CSHF'
        + shifted * 5
        + original[page:graph]
        + expanded
        + original[graph + size :]
    )
    struct.pack_into('<H', legacy, 18, flags | compiler.GESTURES_FLAG)
    struct.pack_into('<I', legacy, 8, len(legacy))
    struct.pack_into('<I', legacy, 20, crc32(legacy[28:]))
    old = bytes(legacy)
    upgraded = resolved.upgrade(old)
    new_graph = resolved.graph_offset(upgraded)
    assert compiler.GRAPH.unpack_from(upgraded, new_graph)[2:] == (
        count + 1,
        switches,
        roots,
        named,
    )
    # Existing nodes remain byte-identical and keep their numerical IDs.
    old_graph = resolved.graph_offset(old)
    for index in range(count):
        before = (
            old_graph
            + struct.unpack_from(
                '<I', old, old_graph + compiler.GRAPH.size + index * 4
            )[0]
        )
        after = (
            new_graph
            + struct.unpack_from(
                '<I', upgraded, new_graph + compiler.GRAPH.size + index * 4
            )[0]
        )
        length = struct.unpack_from('<H', old, before + 2)[0] + 4
        assert upgraded[after : after + length] == old[before : before + length]
    node = (
        new_graph
        + struct.unpack_from(
            '<I', upgraded, new_graph + compiler.GRAPH.size + count * 4
        )[0]
    )
    address = node + 4 + 220
    fields = compiler.GESTURES.unpack_from(upgraded, address + compiler.SWITCH.size)
    absent = [65535] * (len(compiler.GESTURE_EVENTS) - events)
    assert fields == (*([0] * events), *absent, 20, 500, 300, 100)
    assert compiler.SWITCH_HOOKS.unpack_from(
        upgraded, address + compiler.BINDING_HOOKS_OFFSET
    ) == (0, 0, False)
    shifted_fields = compiler.SHIFTED_ACTIONS.unpack_from(
        upgraded, address + compiler.BINDING_SHIFTED_OFFSET
    )
    assert shifted_fields == (*([0] * (events + 2)), *absent, 20, 500, 300, 100)
    decoded = resolved.patch(old, count)
    assert decoded['bindings']
    for event in compiler.GESTURE_EVENTS[events:]:
        assert event not in str(decoded)
    new_page = upgraded.index(b'CPAG')
    shifted_offset = new_page - 5 * compiler.SHIFTED_ACTIONS.size
    assert upgraded[shifted_offset - 4 : shifted_offset] == b'CSHF'
    assert (
        compiler.SHIFTED_ACTIONS.unpack_from(upgraded, shifted_offset) == shifted_fields
    )
    gesture_offset = shifted_offset - 4 - 5 * compiler.GESTURES.size
    assert compiler.GESTURES.unpack_from(upgraded, gesture_offset) == fields


@pytest.mark.parametrize('version', [0, 35, 37, compiler.DOCUMENT_VERSION + 1, 65535])
def test_unsupported_setup_versions_are_rejected(
    current_setup: bytes, version: int
) -> None:
    unsupported = bytearray(current_setup)
    struct.pack_into('<H', unsupported, 4, version)
    with pytest.raises(ValueError, match='header/checksum'):
        resolved.upgrade(bytes(unsupported))


def test_corrupt_setup_is_not_upgraded(current_setup: bytes) -> None:
    corrupt = bytearray(current_setup)
    corrupt[-1] ^= 1
    with pytest.raises(ValueError, match='header/checksum'):
        resolved.upgrade(bytes(corrupt))


ROOT = Path(__file__).parents[1] / 'firmware/test'
