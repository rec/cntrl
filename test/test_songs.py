"""Songs serialize both selection kinds and exchange with the real C++ receiver."""

import os
import pty
import struct
import subprocess
from pathlib import Path
from unittest.mock import Mock
from zlib import crc32

import pytest

from cntrl.patch_switcher import compiler, models, songs, transport


def library() -> songs.Library:
    return songs.Library(
        instruments={
            'piano': {
                'instrument': 'Piano',
                'profile': 'default',
                'channel': 2,
                'output': 'din',
            }
        },
        definitions=models.Definitions(
            profiles=[models.InstrumentProfile(instrument='Piano', name='default')]
        ),
        songs=[
            songs.Song(
                name='Simple',
                patches=[
                    {
                        'type': 'program-change',
                        'instrument': 'piano',
                        'program': 34,
                    }
                ],
            ),
            songs.Song(
                name='Linked',
                patches=[
                    {
                        'document': 'pman',
                        'patch': 'clean',
                    }
                ],
            ),
        ],
        setlists=[songs.Setlist(name='Tonight', songs=['Simple', 'Linked', 'Simple'])],
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
    assert restored.setlists[0].songs == ['Simple', 'Linked', 'Simple']
    assert (
        songs.encode(
            restored,
            target,
            COMPANIONS,
        )
        == data
    )


def test_names_are_unique(target: models.Target) -> None:
    value = library()
    for field, items in (('songs', value.songs), ('setlists', value.setlists)):
        with pytest.raises(ValueError, match='unique'):
            songs.encode(value.model_copy(update={field: [items[0], items[0]]}), target)


def test_setlists_store_names_without_resolving_them(target: models.Target) -> None:
    value = library().model_copy(
        update={'setlists': [songs.Setlist(name='Later', songs=['Missing'])]}
    )
    assert (
        songs.decode(songs.encode(value, target, COMPANIONS)).setlists == value.setlists
    )


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
                    name='First',
                    patches=[
                        {
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
                    'profile': 'default',
                    'channel': 3,
                    'output': 'din',
                }
            },
            'songs': [
                songs.Song(
                    name='Other',
                    patches=[
                        {
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
    end = 8 + songs.SELECTION.size + songs.SONG.size
    position = list(songs.SELECTION.unpack_from(right, 8))
    position[3] = 1
    merged = (
        struct.pack('<II', 2, 0)
        + left[8:end]
        + songs.SELECTION.pack(*position)
        + right[8 + songs.SELECTION.size : end]
        + b'CSP5'
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
                name='Nested visit',
                patches=[
                    {
                        'patches': [
                            {
                                'type': 'control-change',
                                'output': 'din',
                                'channel': 1,
                                'controller': 64,
                                'value': 1,
                            },
                            {
                                'patches': [
                                    {
                                        'operation': 'brightness',
                                        'value': 150,
                                    },
                                    {
                                        'operation': 'stock-firmware',
                                        'exit_buttons': 'A+F',
                                    },
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
                        'type': 'control-change',
                        'output': 'din',
                        'channel': 1,
                        'controller': 67,
                        'value': 4,
                    },
                ],
            )
        ],
        setlists=[songs.Setlist(name='Set', songs=['Nested visit'])],
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
                songs.Setlist(name=f'Set {i}', songs=[f'Song {j}' for j in range(130)])
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
            # Reject counts that would read beyond the payload.
            malformed = bytearray(data)
            struct.pack_into('<I', malformed, 8 + songs.SELECTION.size + 64, 0xFFFFFFFF)
            client.ok(f'CNTRL SONGS BEGIN {len(malformed)} {crc32(malformed):08x} 1')
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


COMPANIONS = {'pman': {'patches': {'clean': {'type': 'start', 'output': 'din'}}}}


@pytest.mark.parametrize('explicit', [False, True])
def test_song_can_start_with_a_derived_patch_and_reuse_its_parent(
    target: models.Target, explicit: bool
) -> None:
    base = {'type': 'program-change', 'instrument': 'piano', 'program': 7, 'volume': 80}
    variation: dict[str, object] = {'parent': 'A', 'volume': 100, 'display_name': "A'"}
    library = songs.Library.model_validate(
        {
            'instruments': {'piano': {'output': 'din', 'channel': 2}},
            'patches': {
                'A′': variation | ({'type': 'derived'} if explicit else {}),
                'A': base,
            },
            'songs': [
                {
                    'name': 'Example',
                    'patches': [{'patch': k} for k in ('A′', 'A', 'A′')],
                }
            ],
        }
    )
    data = songs.encode(library, target)
    restored = songs.decode(data)
    assert restored.patches['A′'] == variation
    assert [p['patch'] for p in restored.songs[0].patches] == ['A′', 'A', 'A′']
    assert songs.encode(restored, target) == data

    for program in (7, 12):
        base['program'] = program
        restored.patches['A'] = base
        data = songs.encode(restored, target)
        # The browser records in the embedded setup are the executable selections.
        source = data[data.index(b'CSW1') :]
        start = 76 + 68 + 68  # One instrument and one patch cycle.
        assert [
            (source[start + i * 82 + 4] + 1, source[start + i * 82 + 7])
            for i in range(3)
        ] == [(program, 100), (program, 80), (program, 100)]


def test_empty_song_library_downloads() -> None:
    data = songs.encode(songs.Library())
    client = Mock(spec=transport.SetupClient)
    client.request.side_effect = [
        f'CNTRL SONGS {len(data)} {crc32(data):08x}',
        'CNTRL DATA ' + data.hex(),
    ]
    assert songs.download(client) == data
    assert not songs.decode(data).songs
