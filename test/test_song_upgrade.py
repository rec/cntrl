"""Song upgrades retain persistent selections while replacing embedded setups."""

import struct
from pathlib import Path

import pytest

from cntrl.patch_switcher import binary_format, compiler, models, resolved, songs


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))


@pytest.mark.parametrize('version', [36, 38])
def test_embedded_legacy_setup_upgrade_keeps_song_bytes_and_node_ids(
    version: int,
    target: models.Target,
) -> None:
    original = bytearray(bytes.fromhex((ROOT / 'setup36.golden').read_text()))
    # Formats 36 and 38 share these historical records; this setup has no looper.
    struct.pack_into('<H', original, 4, version)
    source = bytes(original)
    graph = resolved.graph_offset(source)
    count = compiler.GRAPH.unpack_from(source, graph)[2]
    assert compiler.HEADER.unpack_from(source)[6] >= 2
    node = struct.unpack_from(
        '<H', source, graph + compiler.GRAPH.size + count * 4 + 2
    )[0]
    assert node > 0
    first = songs.SELECTION.pack(
        bytes(64), bytes(64), songs.label_text('Stored A'), 0, node
    )
    second = songs.SELECTION.pack(
        bytes(64), bytes(64), songs.label_text('Stored B'), 1, node
    )
    prefix = (
        struct.pack('<II', 1, 1)
        + first
        + songs.SONG.pack(songs.text('Saved', 64), 2)
        + second
        + songs.SET.pack(songs.text('Tonight', 64), 2)
        + songs.text('Saved', 64) * 2
        + b'CSP5'
    )
    library = prefix + (struct.pack('<I', len(source)) + source) * 2
    upgraded = songs.upgrade(library, {}, target=target)
    core = binary_format.core(upgraded, binary_format.Kind.songs)
    assert core[: len(prefix)] == prefix
    expected = binary_format.wrap(
        resolved.refresh_memory(resolved.upgrade(source), {}, target=target)
    )
    assert len(expected) > len(source)
    offset = len(prefix)
    for _ in range(2):
        length = struct.unpack_from('<I', core, offset)[0]
        assert length == len(expected)
        offset += 4
        assert core[offset : offset + length] == expected
        offset += length
    assert offset == len(core)
    decoded = songs.decode(upgraded)
    assert decoded.songs[0].name == 'Saved'
    assert len(decoded.songs[0].patches) == 2
    assert decoded.setlists[0].songs == ['Saved', 'Saved']
    assert songs.upgrade(upgraded, {}, target=target) == upgraded


def test_empty_song_library_upgrade_preserves_its_bytes(target: models.Target) -> None:
    source = songs.encode(songs.Library())
    assert songs.upgrade(source, {}, target=target) == source


def test_upgrade_rejects_truncated_embedded_sources(target: models.Target) -> None:
    source = (
        binary_format.core(songs.encode(songs.Library()), binary_format.Kind.songs)
        + struct.pack('<I', 10)
        + b'CSW1'
    )
    with pytest.raises(ValueError, match='truncated song Patch source'):
        songs.upgrade(source, {}, target=target)


def test_current_song_sources_keep_their_calculated_memory_and_bytes() -> None:
    target = models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))
    source = songs.encode(
        songs.Library(
            songs=[
                songs.Song(
                    name='Current', patches=[{'type': 'delay', 'duration': '1 ms'}]
                )
            ]
        ),
        target,
    )
    assert source[source.index(b'CSP5') + 8 :].startswith(binary_format.MAGIC)
    upgraded = songs.upgrade(source, {}, target=target)
    assert songs.decode(upgraded) == songs.decode(source)
    assert songs.upgrade(upgraded, {}, target=target) == upgraded


def test_captured_references_follow_the_reordered_deployed_bundle() -> None:
    target = models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))
    document = {
        'patches': {
            'p': {
                'type': 'control-change',
                'output': 'din',
                'channel': 1,
                'controller': 7,
                'value': 64,
            }
        }
    }
    library = songs.encode(
        songs.Library(
            songs=[
                songs.Song(name='Saved', patches=[{'document': 'Zulu', 'patch': 'p'}])
            ]
        ),
        target,
        {'Zulu': document},
    )
    linked = songs.reindex(library, ['Aardvark', 'Zulu'])
    assert songs.decode(linked) == songs.decode(library)
    core = binary_format.core(linked, binary_format.Kind.songs)
    captured = core[core.index(b'CSP5') + 8 :]
    graph = resolved.graph_offset(captured)
    core_source = binary_format.core(captured)
    graph -= binary_format.core_offset(captured)
    count = compiler.GRAPH.unpack_from(core_source, graph)[2]
    references = [
        graph
        + struct.unpack_from('<I', core_source, graph + compiler.GRAPH.size + i * 4)[0]
        for i in range(count)
    ]
    assert [
        struct.unpack_from('<H', core_source, n + 4)[0]
        for n in references
        if core_source[n] == 6
    ] == [1]
    annotated = binary_format.Document(
        kind=binary_format.Kind.songs,
        records=[
            *binary_format.unpack(library, binary_format.Kind.songs).records,
            binary_format.Record(tag=399, data=b'opaque references'),
        ],
    ).pack()
    with pytest.raises(ValueError, match='opaque metadata'):
        songs.reindex(annotated, ['Aardvark', 'Zulu'])


ROOT = Path(__file__).parents[1] / 'firmware/test'
