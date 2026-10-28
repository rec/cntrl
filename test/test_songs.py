"""Songs serialize both selection kinds and exchange with the real C++ receiver."""

import os
import pty
import struct
import subprocess
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.patch_switcher import compiler, models, songs, transport


def library() -> songs.Library:
    return songs.Library(
        instruments={
            'piano': {
                'instrument': 'Piano',
                'setup': 'default',
                'channel': 2,
                'output': 'din',
            }
        },
        definitions=models.Definitions(
            setups=[models.Setup(instrument='Piano', name='default')]
        ),
        songs=[
            songs.Song(
                id=1,
                name='Simple',
                patches=[
                    {
                        'id': 10,
                        'type': 'program-change',
                        'instrument': 'piano',
                        'program': 34,
                    }
                ],
            ),
            songs.Song(
                id=2,
                name='Linked',
                patches=[
                    {
                        'id': 11,
                        'type': 'reference',
                        'document': 'pman',
                        'patch': 'clean',
                    }
                ],
            ),
        ],
        setlists=[songs.Setlist(id=3, name='Tonight', songs=[1, 2, 1])],
    )


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(
            Path(__file__).resolve().parents[1] / 'firmware/test/target.json'
        )
    )


def test_inline_and_reference_selections_and_repeated_occurrences_round_trip(
    target: models.Target,
) -> None:
    data = songs.encode(
        library(),
        target,
        COMPANIONS,
    )
    restored = songs.decode(data)
    assert restored.songs == library().songs
    assert restored.setlists[0].songs == [1, 2, 1]
    assert len(set(restored.setlists[0].entry_ids)) == 3
    assert (
        songs.encode(
            restored,
            target,
            COMPANIONS,
        )
        == data
    )
    # Deleted IDs are never reused after a computer round-trip.
    restored = restored.model_copy(update={'next_id': 1000})
    assert (
        songs.decode(
            songs.encode(
                restored,
                target,
                COMPANIONS,
            )
        ).next_id
        == 1000
    )


def test_missing_songs_and_duplicate_identities_are_rejected() -> None:
    value = library()
    with pytest.raises(ValueError, match='unknown song'):
        songs.encode(
            value.model_copy(
                update={'setlists': [songs.Setlist(id=3, name='Bad', songs=[999])]}
            )
        )
    with pytest.raises(ValueError, match='unique'):
        songs.encode(
            value.model_copy(update={'setlists': [songs.Setlist(id=1, name='Bad')]})
        )
    with pytest.raises(ValueError, match='patches'):
        songs.Song.model_validate({'id': 1, 'name': 'Missing'})


@pytest.mark.parametrize('alias', ['piano', 'p' * 31])
def test_captured_sources_with_colliding_instrument_aliases_export_independently(
    target: models.Target,
    alias: str,
) -> None:
    first = library().model_copy(
        update={
            'instruments': {alias: library().instruments['piano']},
            'songs': [
                songs.Song(
                    id=1,
                    name='First',
                    patches=[
                        {
                            'id': 10,
                            'type': 'program-change',
                            'instrument': alias,
                            'program': 34,
                        }
                    ],
                )
            ],
            'setlists': [],
        }
    )
    second = first.model_copy(
        update={
            'instruments': {
                alias: {
                    'instrument': 'Piano',
                    'setup': 'default',
                    'channel': 3,
                    'output': 'din',
                }
            },
            'songs': [
                songs.Song(
                    id=2,
                    name='Other',
                    patches=[
                        {
                            'id': 11,
                            'type': 'program-change',
                            'instrument': alias,
                            'program': 35,
                        }
                    ],
                )
            ],
        }
    )
    left, right = songs.encode(first, target), songs.encode(second, target)
    end = 12 + songs.SELECTION.size + songs.SONG.size
    position = list(songs.SELECTION.unpack_from(right, 12))
    position[3] = 1
    merged = (
        struct.pack('<III', 12, 2, 0)
        + left[12:end]
        + songs.SELECTION.pack(*position)
        + right[12 + songs.SELECTION.size : end]
        + b'CSP4'
        + left[end + 4 :]
        + right[end + 4 :]
    )
    restored = songs.decode(merged)
    keys = list(restored.instruments)
    assert all(len(k) <= 31 for k in keys)
    assert restored.instruments[keys[0]]['channel'] == 2
    assert restored.instruments[keys[1]]['channel'] == 3
    assert restored.songs[0].patches[0]['instrument'] == keys[0]
    assert restored.songs[1].patches[0]['instrument'] == keys[1]
    assert songs.decode(songs.encode(restored, target)).songs == restored.songs


def test_nested_stock_return_finishes_its_position_before_advancing(
    tmp_path: Path, target: models.Target
) -> None:
    target = target.model_copy(
        update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')}
    )
    selection = songs.Library(
        songs=[
            songs.Song(
                id=1,
                name='Nested visit',
                patches=[
                    {
                        'id': 20,
                        'type': 'sequence',
                        'patches': [
                            {
                                'type': 'control-change',
                                'output': 'din',
                                'channel': 1,
                                'controller': 64,
                                'value': 1,
                            },
                            {
                                'type': 'sequence',
                                'patches': [
                                    {
                                        'type': 'state',
                                        'operation': 'brightness',
                                        'value': 150,
                                    },
                                    {'type': 'stock-firmware', 'exit_buttons': 'A+F'},
                                    {
                                        'type': 'control-change',
                                        'output': 'din',
                                        'channel': 1,
                                        'controller': 65,
                                        'value': 2,
                                    },
                                ],
                            },
                            {
                                'type': 'control-change',
                                'output': 'din',
                                'channel': 1,
                                'controller': 66,
                                'value': 3,
                            },
                        ],
                    },
                    {
                        'id': 21,
                        'type': 'control-change',
                        'output': 'din',
                        'channel': 1,
                        'controller': 67,
                        'value': 4,
                    },
                ],
            )
        ],
        setlists=[songs.Setlist(id=2, name='Set', songs=[1], entry_ids=[3])],
    )
    data = songs.encode(selection, target)
    assert songs.encode(songs.decode(data), target) == data
    image = tmp_path / 'songs.bin'
    image.write_bytes(data)
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / 'song-executor'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_song_executor.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable), str(image)], check=True)


@pytest.mark.parametrize('large', [False, True])
def test_song_library_transfer_against_cpp_receiver(
    tmp_path: Path, large: bool, target: models.Target
) -> None:
    root = Path(__file__).resolve().parents[1]
    peer = tmp_path / 'songs-peer'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/songs_server.cpp'),
            '-o',
            str(peer),
        ],
        check=True,
    )
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    value = library()
    if large:
        value = songs.Library(
            songs=[
                songs.Song(
                    id=i + 1,
                    name=f'Song {i}',
                    patches=[
                        {
                            'type': 'control-change',
                            'output': 'din',
                            'channel': 1,
                            'controller': 64,
                            'value': 127,
                        }
                    ],
                )
                for i in range(130)
            ],
            setlists=[
                songs.Setlist(id=131 + i, name=f'Set {i}', songs=list(range(1, 131)))
                for i in range(20)
            ],
        )
    data = songs.encode(
        value,
        target,
        COMPANIONS,
    )
    assert (
        songs.encode(
            songs.decode(data),
            target,
            COMPANIONS,
        )
        == data
    )
    try:
        with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
            client = transport.SetupClient(connection)
            client.ok(f'CNTRL SONGS BEGIN {len(data)} {crc32(data):08x} 0')
            for i in range(0, len(data), 128):
                client.ok('CNTRL SONGS DATA ' + data[i : i + 128].hex())
            client.ok('CNTRL SONGS END')
            assert songs.download(client) == data
            with pytest.raises(ValueError, match='songs'):
                client.ok('CNTRL SONGS END')
            with pytest.raises(ValueError, match='songs'):
                client.ok(f'CNTRL SONGS BEGIN {len(data)} {crc32(data):08x} 0')
            assert songs.download(client) == data
            if not large:
                for duplicate in (True, False):
                    malformed = bytearray(data)
                    if duplicate:
                        struct.pack_into('<I', malformed, 12 + songs.SELECTION.size, 2)
                    else:
                        struct.pack_into(
                            '<I',
                            malformed,
                            12
                            + 2 * (songs.SONG.size + songs.SELECTION.size)
                            + songs.SET.size
                            + 4,
                            3,
                        )
                    client.ok(
                        f'CNTRL SONGS BEGIN {len(malformed)} {crc32(malformed):08x} 1'
                    )
                    for i in range(0, len(malformed), 128):
                        client.ok('CNTRL SONGS DATA ' + malformed[i : i + 128].hex())
                    with pytest.raises(ValueError, match='songs'):
                        client.ok('CNTRL SONGS END')
                    assert songs.download(client) == data
            # An incomplete replacement cannot change the committed library.
            client.ok(f'CNTRL SONGS BEGIN {len(data)} 00000000 1')
            client.ok('CNTRL SONGS DATA ' + data[:128].hex())
            with pytest.raises(ValueError, match='songs'):
                client.ok('CNTRL SONGS END')
            assert songs.download(client) == data
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


COMPANIONS = {
    'pman': {'patches': {'clean': {'type': 'midi', 'output': 'din', 'bytes': [250]}}}
}
