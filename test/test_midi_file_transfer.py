"""Named MIDI files use staged, checked transfers over the production USB frames."""

from pathlib import Path
from zlib import crc32

import pytest
from native_peer import NativePeer

from cntrl.patch_switcher import transport
from scripts import native_tests


@pytest.fixture(scope='module')
def peer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    root = Path(__file__).parents[1]
    program = tmp_path_factory.mktemp('midi-file-peer') / 'peer'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/midi_file_server.cpp'),
            '-o',
            str(program),
        ]
    )
    return program


def test_copy_is_byte_exact_and_requires_explicit_overwrite(
    peer: Path, tmp_path: Path
) -> None:
    data = b'MThd' + bytes(range(128)) * 80
    with NativePeer([str(peer), str(tmp_path), '']) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            with pytest.raises(FileNotFoundError):
                client.download_midi('A song.mid')
            client.copy_midi('A song.mid', data, False)
            assert client.download_midi('A song.mid') == data
            client.copy_midi('A song.mid', data, False)
            with pytest.raises(ValueError, match='exists with different contents'):
                client.copy_midi('A song.mid', b'new bytes', False)
            assert client.download_midi('A song.mid') == data
            client.copy_midi('A song.mid', b'new bytes', True)
    assert (tmp_path / 'cntrl/midi/A song.mid').read_bytes() == b'new bytes'
    assert [p.name for p in (tmp_path / 'cntrl/midi').iterdir()] == ['A song.mid']


@pytest.mark.parametrize('failure', ['write', 'sync', 'rename'])
def test_failed_replacement_keeps_the_previous_file(
    peer: Path, tmp_path: Path, failure: str
) -> None:
    folder = tmp_path / 'cntrl/midi'
    folder.mkdir(parents=True)
    original = folder / 'song.mid'
    original.write_bytes(b'original')
    with NativePeer([str(peer), str(tmp_path), failure]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            with pytest.raises(ValueError, match='midi-file-'):
                transport.SetupClient(connection).copy_midi(
                    'song.mid', b'replacement', True
                )
    assert original.read_bytes() == b'original'
    assert list(folder.iterdir()) == [original]


@pytest.mark.parametrize('failure', ['read', 'corrupt'])
def test_failed_readback_is_reported(peer: Path, tmp_path: Path, failure: str) -> None:
    with NativePeer([str(peer), str(tmp_path), failure]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            expected = 'midi-file-read' if failure == 'read' else 'read-back mismatch'
            with pytest.raises(ValueError, match=expected):
                transport.SetupClient(connection).copy_midi(
                    'song.mid', b'MIDI bytes', False
                )


def test_cancelled_or_corrupt_upload_does_not_publish(
    peer: Path, tmp_path: Path
) -> None:
    name = b'song.mid'.hex()
    data = b'MIDI bytes'
    with NativePeer([str(peer), str(tmp_path), '']) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            assert (
                client.request(
                    f'CNTRL MIDI_FILE BEGIN {name} {len(data)} {crc32(data):08x} 0'
                )
                == 'CNTRL SEND'
            )
            client.send_data(transport.Kind.midi_file, data[:1])
        assert list((tmp_path / 'cntrl/midi').iterdir()) == []
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            client.request(f'CNTRL MIDI_FILE BEGIN {name} {len(data)} 00000000 0')
            client.send_data(transport.Kind.midi_file, data)
            with pytest.raises(ValueError, match='midi-file-checksum'):
                client.ok('CNTRL MIDI_FILE END')
        assert list((tmp_path / 'cntrl/midi').iterdir()) == []


@pytest.mark.parametrize(
    'name',
    [
        '../escape.mid',
        '..',
        '/outside.mid',
        '.cntrl-upload-song.mid',
        '.CNTRL-upload-song.mid',
    ],
)
def test_receiver_restricts_uploads_to_user_midi_basenames(
    peer: Path, tmp_path: Path, name: str
) -> None:
    with NativePeer([str(peer), str(tmp_path), '']) as port:
        with transport.SerialConnection(Path(port)) as connection:
            with pytest.raises(ValueError, match='midi-file-name'):
                transport.SetupClient(connection).request(
                    f'CNTRL MIDI_FILE BEGIN {name.encode().hex()} 4 00000000 0'
                )
    assert not (tmp_path / 'cntrl/midi').exists()
