"""Songs serialize both selection kinds and exchange with the real C++ receiver."""

import os
import pty
import struct
import subprocess
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.patch_switcher import songs, transport


def library() -> songs.Library:
    return songs.Library(
        songs=[
            songs.Song(
                id=1,
                name='Simple',
                patches=[
                    songs.ProgramChange(id=10, output='din', channel=2, program=34)
                ],
            ),
            songs.Song(
                id=2,
                name='Linked',
                patches=[
                    songs.Reference(
                        id=11, document='pman', instrument='pman', program=7, bank=0
                    )
                ],
            ),
        ],
        setlists=[songs.Setlist(id=3, name='Tonight', songs=[1, 2, 1])],
    )


def test_inline_and_reference_selections_and_repeated_occurrences_round_trip() -> None:
    data = songs.encode(library())
    restored = songs.decode(data)
    assert restored.songs == library().songs
    assert restored.setlists[0].songs == [1, 2, 1]
    assert len(set(restored.setlists[0].entry_ids)) == 3
    assert songs.encode(restored) == data
    # Deleted IDs are never reused after a computer round-trip.
    restored = restored.model_copy(update={'next_id': 1000})
    assert songs.decode(songs.encode(restored)).next_id == 1000


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


@pytest.mark.parametrize('large', [False, True])
def test_song_library_transfer_against_cpp_receiver(
    tmp_path: Path, large: bool
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
                    patches=[songs.ProgramChange(output='din', channel=1, program=1)],
                )
                for i in range(130)
            ],
            setlists=[
                songs.Setlist(id=131 + i, name=f'Set {i}', songs=list(range(1, 131)))
                for i in range(20)
            ],
        )
    data = songs.encode(value)
    assert songs.encode(songs.decode(data)) == data
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
