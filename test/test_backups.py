"""Backup binaries stay authoritative and must match the current format."""

from pathlib import Path

import pytest
import tyro

from cntrl.patch_switcher import (
    backups,
    cli,
    compiler,
    history,
    models,
    resolved,
    songs,
)


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'firmware/test/target.json')
    )


def test_backup_reader_preserves_exact_setup_bytes(tmp_path: Path) -> None:
    data = bytes.fromhex(
        (Path(__file__).parents[1] / 'firmware/test/show.golden').read_text()
    )
    folder = tmp_path / 'setups'
    folder.mkdir()
    (folder / 'Evening.bin').write_bytes(data)
    (tmp_path / 'songs.bin').write_bytes(b'unrelated library bytes')
    assert backups.read_setup_binaries(tmp_path) == {'Evening': data}


def test_backup_reader_rejects_an_old_document_format(tmp_path: Path) -> None:
    data = bytearray.fromhex(
        (Path(__file__).parents[1] / 'firmware/test/show.golden').read_text()
    )
    data[4:6] = (35).to_bytes(2, 'little')
    folder = tmp_path / 'setups'
    folder.mkdir()
    (folder / 'Evening.bin').write_bytes(data)
    with pytest.raises(ValueError, match='invalid setup header'):
        backups.read_setup_binaries(tmp_path)


def test_backup_command_accepts_a_destination_and_port() -> None:
    options = tyro.cli(
        cli.SetupCLI,
        args=['backup', 'backups/before-flash', '--port', '/unused'],
        config=(tyro.conf.OmitSubcommandPrefixes,),
    )
    assert isinstance(options.command, backups.Backup)
    assert options.command.directory == Path('backups/before-flash')
    assert options.command.port == Path('/unused')


def test_backup_refuses_to_overwrite_an_existing_directory(tmp_path: Path) -> None:
    saved = tmp_path / 'selected.txt'
    saved.write_text('Evening')
    with pytest.raises(FileExistsError):
        backups.backup_pedal(tmp_path, Path('/unused'))
    assert saved.read_text() == 'Evening'
    assert not (tmp_path / 'setups').exists()


def test_restore_preparation_upgrades_setups_and_history_without_changing_backup(
    tmp_path: Path,
    target: models.Target,
) -> None:
    import tomlkit

    original = tmp_path / 'before'
    (original / 'setups/fonts').mkdir(parents=True)
    (original / 'history/Evening').mkdir(parents=True)
    data = bytes.fromhex(
        (Path(__file__).parents[1] / 'firmware/test/setup36.golden').read_text()
    )
    (original / 'setups/Evening.bin').write_bytes(data)
    (original / 'setups/fonts/12345678.cfont').write_bytes(b'font')
    (original / 'history/Evening/3.bin').write_bytes(data)
    revision = history.Revision(
        number=3, modified=12345, length=len(data), label='Take'
    )
    (original / 'history.toml').write_text(
        tomlkit.dumps(
            {
                'revisions': [
                    revision.model_dump()
                    | {'name': 'Evening', 'number': '3', 'version': 36}
                ]
            }
        )
    )
    (original / 'selected.txt').write_text('Evening')
    (original / 'songs.bin').write_bytes(songs.encode(songs.Library()))
    saved = {
        p.relative_to(original): p.read_bytes()
        for p in original.rglob('*')
        if p.is_file()
    }
    prepared = backups.prepare_restore(original, tmp_path / 'restore', target)
    assert {
        p.relative_to(original): p.read_bytes()
        for p in original.rglob('*')
        if p.is_file()
    } == saved
    expected = resolved.refresh_memory(
        resolved.upgrade(data), {}, switches=bool(target.max_switches)
    )
    assert backups.read_setup_binaries(prepared) == {'Evening': expected}
    assert backups.read_history_binaries(prepared) == [
        (
            'Evening',
            revision.model_copy(update={'length': len(expected)}),
            expected,
        )
    ]
    assert (prepared / 'setups/fonts/12345678.cfont').read_bytes() == b'font'
    assert (prepared / 'songs.bin').read_bytes() == saved[Path('songs.bin')]
    with pytest.raises(FileExistsError):
        backups.prepare_restore(original, prepared, target)


def test_restore_preparation_rejects_incomplete_backup_before_creating_output(
    tmp_path: Path,
    target: models.Target,
) -> None:
    original = tmp_path / 'before'
    (original / 'setups').mkdir(parents=True)
    (original / 'songs.bin').write_bytes(songs.encode(songs.Library()))
    (original / 'selected.txt').write_text('Missing')
    with pytest.raises(ValueError, match='saved selection is absent'):
        backups.prepare_restore(original, tmp_path / 'restore', target)
    assert not (tmp_path / 'restore').exists()


def test_backup_keeps_older_history_raw_and_collects_unreferenced_assets(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    import struct
    from contextlib import nullcontext

    import tomlkit

    from cntrl.patch_switcher import songs, transport

    old = bytearray(116)
    old[:4] = b'CSW1'
    struct.pack_into('<H', old, 4, 27)

    class Client(transport.SetupClient):
        def __init__(self) -> None:
            super().__init__(transport.SerialConnection(Path('/unused')))
            self.listed = False
            self.storage = iter(
                [
                    'CNTRL STORAGE 34359738368 32768000000 - 32768',
                    'CNTRL ENTRY 1 0 D 0 636e74726c',
                    'CNTRL ENTRY 2 1 D 0 666f6e7473',
                    'CNTRL ENTRY 3 2 F 4 31323334353637382e63666f6e74',
                    'CNTRL ENTRY 4 1 D 0 6d6f76696573',
                    'CNTRL ENTRY 5 4 F 5 38373635343332312e62696e',
                    'CNTRL ENTRY 6 1 D 0 686973746f7279',
                    'CNTRL ENTRY 7 6 D 0 4576656e696e67',
                    'CNTRL ENTRY 8 7 D 0 30303030303030303030303030303633',
                    'CNTRL ENTRY 9 4 F 5 64656164626565662e62696e',
                    'CNTRL STORAGE END 9',
                ]
            )

        def current(self) -> str:
            return 'Evening'

        def catalogue(self) -> list[transport.SetupEntry]:
            return [
                transport.SetupEntry(
                    name='Evening', modified=0, length=0, checksum=0, valid=False
                )
            ]

        def request(self, command: str) -> str:
            if command.startswith('CNTRL HISTORY LIST'):
                if self.listed:
                    return 'CNTRL END'
                self.listed = True
                return 'CNTRL REVISION 3 12345 116 536f756e64636865636b'
            if command.startswith('CNTRL STORAGE'):
                return next(self.storage)
            assert command == 'CNTRL STATUS'
            return 'CNTRL READY 34 0 00000000'

        def download(self, name: str, revision: int | None = None) -> bytes:
            assert name == 'Evening' and revision == 3
            return bytes(old)

        def download_font(self, key: int) -> bytes:
            assert key == 0x12345678
            return b'font'

        def download_movie(self, key: int) -> bytes:
            if key == 0xDEADBEEF:
                raise FileNotFoundError('device cannot read the stored movie')
            assert key == 0x87654321
            return b'movie'

    client = Client()
    monkeypatch.setattr(
        transport, 'SerialConnection', lambda *a, **k: nullcontext(None)
    )
    monkeypatch.setattr(transport, 'SetupClient', lambda connection: client)
    monkeypatch.setattr(songs, 'download', lambda client: b'library')
    directory = tmp_path / 'saved'
    backups.backup_pedal(directory, Path('/unused'))
    assert (directory / 'history/Evening/3.bin').read_bytes() == old
    assert not (directory / 'history/Evening/3.toml').exists()
    manifest = tomlkit.parse((directory / 'history.toml').read_text()).unwrap()
    assert manifest['revisions'] == [
        {
            'number': '3',
            'modified': 12345,
            'length': 116,
            'label': 'Soundcheck',
            'name': 'Evening',
            'version': 27,
        }
    ]
    assert (directory / 'setups/fonts/12345678.cfont').read_bytes() == b'font'
    assert (directory / 'setups/movies/87654321.cnsa').read_bytes() == b'movie'
    assert (directory / 'unavailable-assets.txt').read_bytes() == (
        b'/cntrl/movies/deadbeef.bin\n'
    )
    storage = tomlkit.parse((directory / 'storage.toml').read_text()).unwrap()
    assert '/cntrl/history/Evening/0000000000000063' in {
        e['path'] for e in storage['entries']
    }


def test_restore_uploads_saved_song_library_before_restoring_selection(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    from contextlib import nullcontext
    from zlib import crc32

    from cntrl.patch_switcher import models, songs, transport

    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'firmware/test/target.json')
    )
    setup = compiler.compile_show({}, {}, [], target)
    library = songs.encode(songs.Library(setlists=[songs.Setlist(name='Encore')]))
    (tmp_path / 'setups').mkdir()
    (tmp_path / 'setups/Evening.bin').write_bytes(setup)
    (tmp_path / 'selected.txt').write_text('Evening')
    (tmp_path / 'songs.bin').write_bytes(library)

    class Client(transport.SetupClient):
        def __init__(self) -> None:
            super().__init__(transport.SerialConnection(Path('/unused')))
            self.uploaded = bytearray()
            self.saved = b''
            self.selected = ''

        def download_bundle(self) -> bytes:
            return b''

        def memory(self) -> models.TargetMemory:
            return target.memory

        def copy_bundle(self, data: bytes, overwrite: bool) -> None:
            assert data.startswith(b'CSTB') and overwrite

        def request(self, command: str) -> str:
            if command == 'CNTRL SONGS END':
                self.saved = bytes(self.uploaded)
            elif command == 'CNTRL SONGS SIZE':
                return f'CNTRL SONGS {len(self.saved)} {crc32(self.saved):08x}'
            else:
                assert (
                    command.startswith('CNTRL SONGS BEGIN ')
                    or command == 'CNTRL SETUP FINISH'
                )
            return 'CNTRL OK'

        def send_data(self, kind: transport.Kind, data: bytes) -> None:
            assert kind == transport.Kind.songs
            self.uploaded.extend(data)

        def request_data(self, command: str) -> bytes:
            assert command.startswith('CNTRL SONGS READ ')
            offset = int(command[17:])
            return self.saved[offset : offset + self.connection.usb_block_size]

        def select(self, name: str) -> None:
            assert self.saved == library
            self.selected = name

        def current(self) -> str:
            return self.selected

    client = Client()
    monkeypatch.setattr(
        transport, 'SerialConnection', lambda *a, **k: nullcontext(None)
    )
    monkeypatch.setattr(transport, 'SetupClient', lambda connection: client)
    backups.restore_setups(tmp_path, Path('/unused'), target)
    assert client.saved == library and client.selected == 'Evening'
