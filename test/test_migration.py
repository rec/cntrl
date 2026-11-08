"""Offline upgrades retain saved graph edits, movie identity and song ordering."""

import struct
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit

from cntrl.animation import asset
from cntrl.patch_switcher import compiler, migrate, models, resolved, songs


def test_saved_graph_and_edited_volume_survive_schema_change() -> None:
    target = target_descriptor()
    original = edited_setup()
    converted = migrate.upgrade_setup(original, target, {})
    assert struct.unpack_from('<H', converted, 4)[0] == compiler.DOCUMENT_VERSION
    assert graph_bytes(converted) == graph_bytes(original)
    first_cache = 76 + 2 * 68 + 2 * 68
    assert converted[first_cache + 7] == 91
    exported = tomlkit.parse(resolved.export(converted)).unwrap()
    assert exported['patch_cycles']['wind'][0]['volume'] == 91
    assert converted[:4] == b'CSW1'


def test_movie_conversion_updates_binary_and_editable_references() -> None:
    geometry = asset.Geometry(
        width=3, height=2, streams=[asset.Stream(), asset.Stream(mirror_x=True)]
    )
    target = target_descriptor().model_copy(update={'animation': geometry})
    old_movie = (
        struct.pack('<4sHHHHI', b'CNSA', 1, 3, 2, 1, 44)
        + struct.pack('<I', 750)
        + bytes(range(24))
    )
    old_key = crc32(old_movie)
    assets, keys = migrate.upgrade_movies({old_key: old_movie}, geometry)
    original = edited_setup()
    position = original.index(b'CMOV')
    body = bytearray(
        original[:position]
        + struct.pack('<4sHHI', b'CMOV', 1, 0, old_key)
        + original[position + 8 :]
    )
    body[19] |= 1
    original = valid_header(bytes(body))
    converted = migrate.upgrade_setup(original, target, keys)
    with pytest.raises(ValueError, match='referenced movie is absent'):
        migrate.upgrade_setup(original, target, {})
    assert graph_bytes(converted) == graph_bytes(original)
    exported = tomlkit.parse(resolved.export(converted)).unwrap()
    assert exported['splash_movie'] == [f'movies/{keys[old_key]:08x}.cnsa']
    assert asset.inspect_asset(assets[keys[old_key]]).duration_ms == 750
    assert next(iter(assets.values()))[asset.HEADER.size + 4 :] == old_movie[20:]


def test_song_upgrade_keeps_selection_order_and_embedded_edits() -> None:
    original = edited_setup()
    graph = original.index(b'CPAT')
    count = struct.unpack_from('<H', original, graph + 8)[0]
    root = struct.unpack_from('<H', original, graph + compiler.GRAPH.size + count * 4)[
        0
    ]
    selection = songs.SELECTION.pack(bytes(64), bytes(64), b'Verse', 0, root)
    prefix = (
        struct.pack('<II', 1, 1)
        + selection
        + songs.SONG.pack(b'Example', 2)
        + selection
    )
    prefix += songs.SET.pack(b'Show', 2) + b'Example'.ljust(64, b'\0') * 2 + b'CSP5'
    old_library = prefix + struct.pack('<I', len(original)) + original
    converted = migrate.upgrade_songs(old_library, target_descriptor(), {}, ['example'])
    assert converted.startswith(prefix)
    source = converted[len(prefix) + 4 :]
    assert graph_bytes(source) == graph_bytes(original)
    library = songs.decode(converted)
    assert library.setlists[0].songs == ['Example', 'Example']
    assert [p['volume'] for p in library.songs[0].patches] == [91, 91]


def target_descriptor() -> models.Target:
    root = Path(__file__).resolve().parents[1]
    return models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )


def edited_setup() -> bytes:
    root = Path(__file__).resolve().parents[1]
    data = bytearray(bytes.fromhex((root / 'test/data/legacy-34.golden').read_text()))
    _, _, _, _, ni, nc, *_ = compiler.HEADER.unpack_from(data)
    data[76 + ni * 68 + nc * 68 + 7] = 91
    graph = data.index(b'CPAT')
    count = struct.unpack_from('<H', data, graph + 8)[0]
    node = struct.unpack_from('<H', data, graph + compiler.GRAPH.size + count * 4)[0]
    offset = (
        graph
        + struct.unpack_from('<I', data, graph + compiler.GRAPH.size + node * 4)[0]
    )
    assert data[offset] == 1
    data[offset + 9] = 91
    return valid_header(bytes(data))


def valid_header(data: bytes) -> bytes:
    result = bytearray(data)
    struct.pack_into('<I', result, 8, len(result))
    struct.pack_into('<I', result, 20, crc32(result[28:]))
    return bytes(result)


def graph_bytes(data: bytes) -> bytes:
    offset = data.index(b'CPAT')
    length = struct.unpack_from('<I', data, offset + 4)[0]
    return data[offset : offset + length]


def test_switch_gestures_hooks_and_shift_layers_follow_slots_after_upgrade() -> None:
    root = Path(__file__).resolve().parents[1]
    original = bytes.fromhex((root / 'test/data/legacy-switches-34.golden').read_text())
    target = target_descriptor().model_copy(update={'buttons': list('ABCDEF')})
    converted = migrate.upgrade_setup(original, target, {})
    assert graph_bytes(converted) == graph_bytes(original)
    exported = tomlkit.parse(resolved.export(converted)).unwrap()
    for name, button, base in [('fourth', 'D', 40), ('second', 'B', 20)]:
        switch = exported['switches'][name]
        assert switch['button'] == button
        assert switch['gestures']['double_tap']['value'] == base + 1
        assert switch['enter']['action']['value'] == base + 10
        assert switch['exit']['value'] == base + 11
        assert switch['shifted']['on']['value'] == base + 2
        assert switch['shifted']['gestures']['long_press']['value'] == base + 3
