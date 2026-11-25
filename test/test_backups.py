"""Backup binaries stay authoritative and must match the current format."""

import struct
import sys
from contextlib import nullcontext
from pathlib import Path
from unittest.mock import Mock
from zlib import crc32

import pytest
import tomlkit
import tyro

from cntrl.animation import asset
from cntrl.localization import catalogue
from cntrl.patch_chooser import (
    backups,
    binary_format,
    cli,
    compiler,
    compiler_inputs,
    compiler_wire,
    device_globals,
    display,
    graphics,
    history,
    languages,
    models,
    resolved,
    setup_bundle,
    songs,
    storage_info,
    transport,
)


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler_inputs.read_document(
            Path(__file__).parents[1] / 'firmware/test/target.json'
        )
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


@pytest.mark.parametrize('font', [0, 1, 2, 5, 9, 18])
@pytest.mark.parametrize('wrapped', [False, True])
def test_restore_replaces_removed_ui_fonts_without_changing_originals(
    font: int, wrapped: bool, target: models.Target
) -> None:
    current = compiler.compile_show(
        {
            'page_settings': {
                'tempo_font': 'Arimo',
                'status_font': 'Arimo',
                'title_font': 'Arimo',
            }
        },
        {},
        [],
        target,
    )
    original = bytearray(current)
    ni, nc, np = compiler_wire.HEADER.unpack_from(current)[4:7]
    settings = 76 + ni * 68 + nc * 68 + np * 82 + 16
    original[28] = font
    for index in (14, 15, 22):
        struct.pack_into('<H', original, settings + index * 2, font)
    struct.pack_into('<I', original, 20, crc32(original[28:]))
    data = binary_format.wrap(bytes(original)) if wrapped else bytes(original)
    baseline = binary_format.wrap(current) if wrapped else current

    # Backup export can describe the old selection, while retaining its bytes.
    assert resolved.upgrade(data) == data
    pages = tomlkit.parse(resolved.export(data))['page_settings']
    for name in ('tempo_font', 'status_font', 'title_font'):
        assert pages[name] == 'Arimo'
    restored = resolved.prepare_restore(data, {}, target=target)
    assert restored == resolved.prepare_restore(baseline, {}, target=target)
    assert binary_format.core(data)[28] == font
    assert resolved.prepare_restore(restored, {}, target=target) == restored


def test_unknown_ui_font_is_reported_as_invalid_data(target: models.Target) -> None:
    data = bytearray(compiler.compile_show({}, {}, [], target))
    ni, nc, np = compiler_wire.HEADER.unpack_from(data)[4:7]
    status_font = 76 + ni * 68 + nc * 68 + np * 82 + 16 + 15 * 2
    struct.pack_into('<H', data, status_font, 99)
    struct.pack_into('<I', data, 20, crc32(data[28:]))
    with pytest.raises(
        binary_format.DocumentFormatError, match='unknown UI font ID 99'
    ):
        resolved.upgrade(bytes(data))


def test_restore_replaces_retired_fonts_in_compiled_display_styles(
    target: models.Target,
) -> None:
    current = compiler.compile_show(
        {
            'patch_cycles': {'Show': [{'type': 'delay', 'duration': '1 ms'}]},
            'fill_in_styles': False,
        },
        {},
        [],
        target,
    )
    graph = resolved.graph_offset(current)
    presentation = graph + compiler_wire.GRAPH.unpack_from(current, graph)[1]
    styles = display.HEADER.unpack_from(current, presentation)[1]
    assert styles
    original = bytearray(current)
    for index in range(styles):
        address = presentation + display.HEADER.size + index * graphics.SCREEN.size
        assert original[address] == 17
        original[address] = 1
    struct.pack_into('<I', original, 20, crc32(original[28:]))
    assert resolved.prepare_restore(bytes(original), {}, target=target) == (
        resolved.prepare_restore(current, {}, target=target)
    )


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


@pytest.mark.parametrize('command', ['restore', 'verify'])
def test_same_firmware_recovery_accepts_preserved_memory_estimates(
    command: str,
) -> None:
    options = tyro.cli(
        cli.SetupCLI,
        args=[
            command,
            'backups/before-test',
            '--port',
            '/unused',
            '--target',
            'target.toml',
            '--preserve-memory-estimates',
        ],
        config=(tyro.conf.OmitSubcommandPrefixes,),
    )
    assert isinstance(options.command, (backups.Restore, backups.Verify))
    assert options.command.preserve_memory_estimates


def test_preserving_memory_estimates_rejects_a_format_upgrade(
    target: models.Target,
    legacy_setups: dict[int, bytes],
) -> None:
    data = legacy_setups[36]
    with pytest.raises(ValueError, match='requires current-format setups'):
        resolved.prepare_restore(
            data, {}, target=target, preserve_memory_estimates=True
        )


def test_song_restore_preserves_recorded_source_memory_estimates(
    target: models.Target,
) -> None:
    original_library = songs.encode(songs.Library(midi={}), target)
    library = bytearray(binary_format.core(original_library, binary_format.Kind.songs))
    offset = library.index(b'CSP9') + 8
    original = bytes(library[offset:])
    source = bytearray(binary_format.core(original))
    metadata = source.index(b'CMTA') + compiler_wire.METADATA.size - 8
    struct.pack_into('<I', source, metadata, 1)
    struct.pack_into('<I', source, 20, crc32(source[28:]))
    library[offset:] = binary_format.replace_core(original, bytes(source))
    data = binary_format.replace_core(
        original_library, bytes(library), binary_format.Kind.songs
    )
    assert (
        songs.upgrade(data, {}, target=target, preserve_memory_estimates=True) == data
    )
    assert songs.upgrade(data, {}, target=target) != data


def test_backup_refuses_to_overwrite_an_existing_directory(tmp_path: Path) -> None:
    saved = tmp_path / 'selected.txt'
    saved.write_text('Evening')
    with pytest.raises(FileExistsError):
        backups.backup_pedal(tmp_path, Path('/unused'))
    assert saved.read_text() == 'Evening'
    assert not (tmp_path / 'setups').exists()


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_backup_cli_preserves_unexpected_failures(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected backup failure')
    monkeypatch.setattr(backups, 'backup_pedal', Mock(side_effect=failure))
    monkeypatch.setattr(
        sys,
        'argv',
        ['cntrl', 'setup', 'backup', str(tmp_path / 'backup'), '--port', '/unused'],
    )
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure


def test_backup_asset_scan_preserves_unexpected_host_file_failures(
    tmp_path: Path,
) -> None:
    client = Mock(spec=transport.SetupClient)
    client.request.side_effect = [
        'CNTRL STORAGE 65536 65536 32768 4096',
        'CNTRL ENTRY 1 0 D 0 636e74726c',
        'CNTRL ENTRY 2 1 D 0 6d6f76696573',
        'CNTRL ENTRY 3 2 F 5 64656164626565662e62696e',
        'CNTRL STORAGE END 3',
    ]
    failure = FileNotFoundError('unexpected host file missing')
    client.download_movie.side_effect = failure
    with pytest.raises(FileNotFoundError) as caught:
        backups.backup_stored_assets(client, tmp_path)
    assert caught.value is failure
    assert not (tmp_path / 'unavailable-assets.txt').exists()


@pytest.mark.parametrize(
    ('action', 'operation'),
    [
        ('restore', 'restore_setups'),
        ('verify', 'verify_after_flashing'),
        ('convert', 'prepare_restore'),
    ],
)
@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_restore_commands_preserve_unexpected_failures(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    action: str,
    operation: str,
    error_type: type[Exception],
) -> None:
    failure = error_type('unexpected restore failure')
    monkeypatch.setattr(backups, operation, Mock(side_effect=failure))
    target_path = Path(__file__).parents[1] / 'firmware/test/target.json'
    options = (
        ['--destination', str(tmp_path / 'converted')]
        if action == 'convert'
        else ['--port', '/unused']
    )
    monkeypatch.setattr(
        sys,
        'argv',
        [
            'cntrl',
            'setup',
            action,
            str(tmp_path),
            '--target',
            str(target_path),
            *options,
        ],
    )
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure


def test_restore_reports_a_malformed_backup_manifest_before_opening_usb(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    (tmp_path / 'backup.toml').write_text('[invalid')
    target_path = Path(__file__).parents[1] / 'firmware/test/target.json'
    connect = Mock(side_effect=AssertionError('unexpected USB open'))
    monkeypatch.setattr(transport, 'SerialConnection', connect)
    monkeypatch.setattr(
        sys,
        'argv',
        [
            'cntrl',
            'setup',
            'restore',
            str(tmp_path),
            '--port',
            '/unused',
            '--target',
            str(target_path),
        ],
    )
    with pytest.raises(SystemExit, match=r'backup\.toml'):
        cli.main()
    connect.assert_not_called()


def test_restore_preparation_upgrades_setups_and_history_without_changing_backup(
    tmp_path: Path,
    target: models.Target,
    legacy_setups: dict[int, bytes],
) -> None:
    import tomlkit

    original = tmp_path / 'before'
    (original / 'setups/fonts').mkdir(parents=True)
    (original / 'history/Evening').mkdir(parents=True)
    data = legacy_setups[36]
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
    undo = songs.encode(songs.Library(midi={}), target)
    (original / 'songs-undo.bin').write_bytes(undo)
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
    expected = binary_format.wrap(resolved.prepare_restore(data, {}, target=target))
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
    assert (prepared / 'songs-undo.bin').read_bytes() == songs.upgrade(
        undo, {}, target=target
    )
    backups.check_conversion(prepared)
    (prepared / 'setups/fonts/12345678.cfont').write_bytes(b'changed')
    with pytest.raises(ValueError, match='checksum differs'):
        backups.check_conversion(prepared)
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


@pytest.mark.parametrize('empty', [False, True])
def test_conversion_preserves_container_metadata_and_keeps_original_bytes(
    tmp_path: Path, target: models.Target, empty: bool
) -> None:
    original = tmp_path / 'original'
    (original / 'setups').mkdir(parents=True)
    source = {'a': {}}
    data = binary_format.wrap(
        compiler.compile_show({}, {}, [], target, documents=source)
    )
    metadata = [binary_format.Record(tag=399, data=b'future nested metadata')]
    bundle = (
        setup_bundle.build({'a': data}, source, target.memory)
        .model_copy(update={'metadata': metadata})
        .pack()
    )
    if empty:
        bundle = setup_bundle.SetupBundle(
            setups={'a': b''}, page_ram2=[0], memory=target.memory, metadata=metadata
        ).pack()
    library = songs.encode(songs.Library())
    library = binary_format.Document(
        kind=binary_format.Kind.songs,
        records=[
            *binary_format.unpack(library, binary_format.Kind.songs).records,
            *metadata,
        ],
    ).pack()
    files = [
        ('bundle.bin', bundle),
        ('songs.bin', library),
        ('songs-undo.bin', library),
        ('selected.txt', b'' if empty else b'a'),
    ]
    if not empty:
        files.append(('setups/a.bin', data))
    for name, content in files:
        (original / name).write_bytes(content)
    converted = backups.prepare_restore(original, tmp_path / 'converted', target)
    assert (original / 'bundle.bin').read_bytes() == bundle
    for name, kind in [
        ('bundle.bin', binary_format.Kind.bundle),
        ('songs.bin', binary_format.Kind.songs),
        ('songs-undo.bin', binary_format.Kind.songs),
    ]:
        assert (
            binary_format.unpack((converted / name).read_bytes(), kind).records[1:]
            == metadata
        )
    backups.check_conversion(converted)


def test_recording_high_water_uses_valid_sectors_and_preserves_newer_recordings(
    tmp_path: Path,
) -> None:
    good = b'[slot1]\nstarts = 255\ncheck = "ffffffffffffff00"\n'
    (tmp_path / 'starts.toml').write_bytes(
        b'corrupt'.ljust(512, b'!') + good.ljust(511, b' ') + b'\n'
    )
    assert backups.recording_high_water(tmp_path) == 255
    midi = tmp_path / 'setups/midi'
    midi.mkdir(parents=True)
    (midi / '300-3.mid').write_bytes(b'preserved')
    assert backups.recording_high_water(tmp_path) == 300
    (tmp_path / 'starts.toml').write_bytes(b'corrupt')
    with pytest.raises(ValueError, match='no valid startup counter'):
        backups.recording_high_water(tmp_path)


def test_deployment_budget_includes_allocation_and_retained_recovery_space() -> None:
    report = storage_info.parse_storage_info('CNTRL STORAGE 65536 65536 8192 4096')
    backups.check_deployment_space(report, [4096], 1)
    with pytest.raises(ValueError, match='needs 12288 free bytes'):
        backups.check_deployment_space(report, [4097], 1)
    with pytest.raises(ValueError, match='storage scan'):
        backups.check_deployment_space(
            report.model_copy(update={'free_bytes': None}), [1], 0
        )


def test_conversion_checks_undo_source_assets_before_creating_destination(
    tmp_path: Path, target: models.Target
) -> None:
    original = tmp_path / 'before'
    (original / 'setups').mkdir(parents=True)
    (original / 'songs.bin').write_bytes(songs.encode(songs.Library()))
    (original / 'selected.txt').write_text('')
    (original / 'songs-undo.bin').write_bytes(
        songs.encode(
            songs.Library(
                patches={'p': {'operation': 'play-midi-file', 'filename': 'take.mid'}}
            ),
            target,
        )
    )
    with pytest.raises(ValueError, match='midi/take.mid: required asset'):
        backups.prepare_restore(original, tmp_path / 'converted', target)
    assert not (tmp_path / 'converted').exists()


@pytest.mark.parametrize('deleted_setup', [False, True])
def test_backup_keeps_older_history_raw_and_collects_unreferenced_assets(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    deleted_setup: bool,
) -> None:
    import struct
    from contextlib import nullcontext

    import tomlkit

    from cntrl.patch_chooser import songs, transport

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
            if deleted_setup:
                return []
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
                raise transport.UsbFileMissing('device cannot read the stored movie')
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


def test_failed_startup_still_allows_an_incomplete_raw_recovery_backup(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    calibration = (
        b'[configurable-1]\nname = "Boss EV-30, left"\nheel = 118\ntoe = 906\n'
    )

    class Client(transport.SetupClient):
        def __init__(self) -> None:
            super().__init__(transport.SerialConnection(Path('/unused')))
            self.storage = iter(
                [
                    'CNTRL STORAGE 65536 65536 32768 4096',
                    'CNTRL ENTRY 1 0 D 0 636e74726c',
                    'CNTRL ENTRY 2 1 D 0 736574757073',
                    'CNTRL ENTRY 3 2 F 8 302e62696e',
                    f'CNTRL ENTRY 4 1 F {len(calibration)} '
                    + b'calibration.toml'.hex(),
                    'CNTRL STORAGE END 4',
                ]
            )

        def request(self, command: str) -> str:
            if command == 'CNTRL STATUS':
                raise transport.UsbReplyError('CNTRL ERROR incompatible-format')
            if command == 'CNTRL DEPLOY STATUS':
                return 'CNTRL DEPLOY 0 0 0'
            assert command.startswith('CNTRL STORAGE')
            return next(self.storage)

        def download_state(self, path: str) -> bytes:
            if path == '/cntrl/calibration.toml':
                return calibration
            assert path == '/cntrl/setups/0.bin'
            return b'original'

    client = Client()
    monkeypatch.setattr(
        transport, 'SerialConnection', lambda *a, **k: nullcontext(None)
    )
    monkeypatch.setattr(transport, 'SetupClient', lambda connection: client)
    saved = tmp_path / 'recovery'
    backups.backup_pedal(saved, Path('/unused'))
    assert (saved / 'raw/cntrl/setups/0.bin').read_bytes() == b'original'
    assert (saved / 'raw/cntrl/calibration.toml').read_bytes() == calibration
    with pytest.raises(ValueError, match='incomplete'):
        backups.check_conversion(saved)
    assert not (saved / 'backup-pending.toml').exists()


@pytest.mark.parametrize('preserve', [False, True])
@pytest.mark.parametrize('nonempty', [False, True])
@pytest.mark.parametrize('overlay', [None, {}, {0: 88}])
def test_restore_uploads_saved_data_before_restoring_selection(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    preserve: bool,
    nonempty: bool,
    overlay: dict[int, int] | None,
) -> None:
    from contextlib import nullcontext

    from cntrl.patch_chooser import global_overlay, models, songs, transport

    target = models.Target.model_validate(
        compiler_inputs.read_document(
            Path(__file__).parents[1] / 'firmware/test/target.json'
        )
    )
    if overlay is not None:
        (tmp_path / 'device-globals.toml').write_bytes(
            device_globals.dump(global_overlay.Overlay(values=overlay))
        )
    sources: dict[str, object] = {
        'Aardvark': {
            'inline_patches': False,
            'patches': {'borrowed': {'document': 'Zulu', 'patch': 'p'}},
        },
        'Evening': {
            'inline_patches': False,
            'patches': {'borrowed': {'document': 'Zulu', 'patch': 'p'}},
        },
        'Zulu': {
            'patches': {
                'p': {
                    'type': 'control-change',
                    'output': 'din',
                    'channel': 1,
                    'controller': 7,
                    'value': 77,
                }
            }
        },
    }
    backup_sources = (
        {n: s for n, s in sources.items() if n != 'Aardvark'}
        if nonempty
        else {'Evening': {}}
    )
    recorded = bytearray(
        compiler.compile_show(
            backup_sources['Evening'], {}, [], target, documents=backup_sources
        )
    )
    metadata = recorded.index(b'CMTA') + compiler_wire.METADATA.size - 8
    struct.pack_into('<I', recorded, metadata, 1)
    struct.pack_into('<I', recorded, 20, crc32(recorded[28:]))
    compiled = {
        n: compiler.compile_show(s, {}, [], target, documents=backup_sources)
        for n, s in backup_sources.items()
    }
    setup = setup_bundle.reindex(compiled | {'Evening': bytes(recorded)})['Evening']
    expected = setup if preserve else resolved.refresh_memory(setup, {}, target=target)
    library = songs.encode(songs.Library(setlists=[songs.Setlist(name='Encore')]))
    (tmp_path / 'setups').mkdir()
    (tmp_path / 'setups/Evening.bin').write_bytes(setup)
    (tmp_path / 'selected.txt').write_text('Evening')
    (tmp_path / 'songs.bin').write_bytes(library)
    revision = history.Revision(
        number=3, modified=12345, length=len(setup), label='Take'
    )
    (tmp_path / 'history/Evening').mkdir(parents=True)
    (tmp_path / 'history/Evening/3.bin').write_bytes(setup)
    (tmp_path / 'history.toml').write_text(
        tomlkit.dumps(
            {
                'revisions': [
                    revision.model_dump()
                    | {
                        'name': 'Evening',
                        'number': '3',
                        'version': compiler_wire.DOCUMENT_VERSION,
                    }
                ]
            }
        )
    )
    imported: dict[str, bytes] = {}
    current_sources = {n: s for n, s in sources.items() if n != 'Evening'}
    existing = (
        {
            n: compiler.compile_show(s, {}, [], target, documents=current_sources)
            for n, s in current_sources.items()
        }
        if nonempty
        else {}
    )
    current_bundle = (
        setup_bundle.build(existing, current_sources, target.memory).pack()
        if nonempty
        else b''
    )

    def import_revision(
        client: transport.SetupClient, name: str, record: history.Revision, data: bytes
    ) -> None:
        assert record == revision
        imported[name] = data

    class Client(transport.SetupClient):
        def __init__(self) -> None:
            super().__init__(transport.SerialConnection(Path('/unused')))
            self.uploaded = bytearray()
            self.saved = b''
            self.selected = ''
            self.bundle = b''
            self.globals: global_overlay.Overlay | None = None

        def download_bundle(self) -> bytes:
            return current_bundle

        def memory(self) -> models.TargetMemory:
            return target.memory

        def copy_bundle(self, data: bytes, overwrite: bool) -> None:
            assert (
                binary_format.core(data, binary_format.Kind.bundle).startswith(b'CSTB')
                and overwrite
            )
            self.bundle = data

        def request(self, command: str) -> str:
            if command == 'CNTRL FORMAT':
                return f'CNTRL FORMAT {binary_format.DEVELOPMENT_FORMAT} 1 2 3'
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
            assert imported == {'Evening': expected}
            self.selected = name

        def current(self) -> str:
            return self.selected

        def begin_deployment(self) -> int:
            assert not self.saved
            return 1

        def recover_deployment(self) -> bool:
            assert not self.saved
            return True

        def commit_deployment(self, bundle: bytes, songs: bytes) -> None:
            assert bundle == self.bundle and songs == self.saved
            assert self.selected == 'Evening'
            assert self.globals == (
                global_overlay.Overlay(values={0: 25} if overlay is None else overlay)
            )

        def replace_globals(self, values: global_overlay.Overlay) -> None:
            assert self.saved == library and self.selected == 'Evening'
            self.globals = values

    client = Client()

    def snapshot(directory: Path, port: Path, connection: object) -> None:
        directory.mkdir()
        (directory / 'backup-pending.toml').write_text('complete = false')
        (directory / 'device-globals.toml').write_bytes(
            device_globals.dump(global_overlay.Overlay(values={0: 25}))
        )
        backups.write_backup_manifest(directory, complete=True, raw_state=False)

    monkeypatch.setattr(backups, 'backup_pedal', snapshot)
    monkeypatch.setattr(
        transport, 'SerialConnection', lambda *a, **k: nullcontext(None)
    )
    monkeypatch.setattr(transport, 'SetupClient', lambda connection: client)
    monkeypatch.setattr(history, 'import_revision', import_revision)
    backups.restore_setups(
        tmp_path, Path('/unused'), target, preserve_memory_estimates=preserve
    )
    assert client.saved == library and client.selected == 'Evening'
    restored = setup_bundle.unpack(client.bundle)
    assert restored == {
        n: binary_format.wrap(d)
        for n, d in setup_bundle.reindex(existing | {'Evening': expected}).items()
    }
    if nonempty:
        assert resolved.export(restored['Aardvark']) == resolved.export(
            existing['Aardvark']
        )
        assert restored['Aardvark'] != existing['Aardvark']
        assert restored['Evening'] != expected


@pytest.mark.parametrize('converted', [False, True])
@pytest.mark.parametrize('journal_ready', [False, True])
def test_installing_a_converted_collection_retains_an_unreadable_active_root(
    tmp_path: Path,
    target: models.Target,
    monkeypatch: pytest.MonkeyPatch,
    converted: bool,
    journal_ready: bool,
) -> None:
    from cntrl.patch_chooser.setup_sync_store import write_record

    original = tmp_path / 'original'
    (original / 'setups').mkdir(parents=True)
    setup = binary_format.wrap(compiler.compile_show({}, {}, [], target))
    library = songs.encode(songs.Library())
    (original / 'setups/a.bin').write_bytes(setup)
    (original / 'songs.bin').write_bytes(library)
    (original / 'selected.txt').write_text('a')
    directory = (
        backups.prepare_restore(original, tmp_path / 'converted', target)
        if converted
        else original
    )
    operations: list[str] = []
    recovery_backups: list[Path] = []

    class Client(transport.SetupClient):
        bundle: bytes = b''
        selected: str = ''

        def recover_deployment(self) -> bool:
            return journal_ready

        def download_bundle(self) -> bytes:
            pytest.fail('an unreadable active bundle must not be merged')

        def memory(self) -> models.TargetMemory:
            return target.memory

        def begin_deployment(self, *, recovery: bool = False) -> int:
            assert recovery and operations == ['backup']
            operations.append('begin')
            return 123

        def copy_bundle(self, data: bytes, overwrite: bool) -> None:
            assert overwrite and list(setup_bundle.unpack(data)) == ['a']
            self.bundle = data
            operations.append('bundle')

        def ok(self, command: str) -> None:
            assert command == 'CNTRL SETUP FINISH'

        def select(self, name: str) -> None:
            self.selected = name
            operations.append('select')

        def current(self) -> str:
            return self.selected

        def commit_deployment(self, bundle: bytes, data: bytes) -> None:
            assert bundle == self.bundle and data == library and self.selected == 'a'
            operations.append('commit')

    client = Client(transport.SerialConnection(Path('/unused')))

    def snapshot(folder: Path, port: Path, connection: object) -> None:
        operations.append('backup')
        recovery_backups.append(folder)
        folder.mkdir()
        (folder / 'backup-pending.toml').write_text('complete = false')
        (folder / 'original.raw').write_bytes(b'unsupported original journal')
        report = storage_info.parse_storage_info(
            'CNTRL STORAGE 2097152 2097152 1048576 4096'
        )
        write_record(folder / 'storage.toml', report.model_dump(exclude_none=True))
        backups.write_backup_manifest(folder, complete=False, raw_state=True)

    def upload(receiver: transport.SetupClient, data: bytes, overwrite: bool) -> None:
        assert receiver is client and data == library and overwrite
        operations.append('songs')

    monkeypatch.setattr(backups, 'backup_pedal', snapshot)
    monkeypatch.setattr(
        transport, 'SerialConnection', lambda *a, **k: nullcontext(None)
    )
    monkeypatch.setattr(transport, 'SetupClient', lambda connection: client)
    monkeypatch.setattr(songs, 'upload', upload)
    if not converted:
        with pytest.raises(ValueError, match='offline converted backup'):
            backups.restore_setups(directory, Path('/unused'), target)
        assert operations == ['backup']
        return
    backups.restore_setups(directory, Path('/unused'), target)
    assert operations == ['backup', 'begin', 'bundle', 'songs', 'select', 'commit']
    backup = recovery_backups[0]
    assert (backup / 'original.raw').read_bytes() == b'unsupported original journal'
    backups.check_conversion(backup, raw_recovery=True)
    with pytest.raises(ValueError, match='incomplete backup'):
        backups.check_conversion(backup)
    (backup / 'original.raw').write_bytes(b'damaged')
    with pytest.raises(ValueError, match='checksum differs'):
        backups.check_conversion(backup, raw_recovery=True)


class SavedPedal(transport.SetupClient):
    def __init__(
        self, setup: bytes, revision: history.Revision, library: bytes
    ) -> None:
        super().__init__(transport.SerialConnection(Path('/unused')))
        self.status = f'CNTRL READY {compiler_wire.DOCUMENT_VERSION} 0 00000000'
        self.selected = 'Evening'
        self.valid = True
        self.revision = revision
        self.blobs = {
            'setup': setup,
            'font': b'font',
            'movie': b'movie',
            'midi': (
                b'MThd\x00\x00\x00\x06\x00\x00\x00\x01\x01\xe0'
                b'MTrk\x00\x00\x00\x04\x00\xff\x2f\x00'
            ),
            'history': history.normalize_revision('Evening', setup)[1],
            'songs': library,
        }

    def current(self) -> str:
        return self.selected

    def catalogue(self) -> list[transport.SetupEntry]:
        data = self.blobs['setup']
        return [
            transport.SetupEntry(
                name='Evening',
                modified=0,
                length=len(data),
                checksum=crc32(data),
                valid=self.valid,
            )
        ]

    def request(self, command: str) -> str:
        if command == 'CNTRL STATUS':
            return self.status
        if command == 'CNTRL STORAGE SCAN':
            return 'CNTRL STORAGE 1000000 900000 - 512'
        if command == 'CNTRL STORAGE NEXT':
            return 'CNTRL STORAGE END 0'
        if command.startswith('CNTRL HISTORY LIST '):
            before = int(command.rsplit(' ', 1)[1])
            revision = self.revision
            if revision.number >= before:
                return 'CNTRL END'
            return (
                f'CNTRL REVISION {revision.number} {revision.modified} '
                f'{revision.length} {revision.label.encode().hex()}'
            )
        assert command == 'CNTRL SONGS SIZE'
        data = self.blobs['songs']
        return f'CNTRL SONGS {len(data)} {crc32(data):08x}'

    def request_data(self, command: str) -> bytes:
        assert command.startswith('CNTRL SONGS READ ')
        offset = int(command.rsplit(' ', 1)[1])
        return self.blobs['songs'][offset : offset + self.connection.usb_block_size]

    def download(self, name: str, revision: int | None = None) -> bytes:
        assert name == 'Evening'
        if revision is not None:
            assert revision == self.revision.number
        return self.blobs['setup' if revision is None else 'history']

    def download_font(self, key: int) -> bytes:
        assert key == 0x12345678
        return self.blobs['font']

    def download_movie(self, key: int) -> bytes:
        assert key == 0x87654321
        return self.blobs['movie']

    def download_midi(self, name: str) -> bytes:
        assert name == 'take.mid'
        return self.blobs['midi']


@pytest.fixture
def saved_pedal(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, target: models.Target
) -> SavedPedal:
    setup = setup_bundle.reindex(
        {'Evening': compiler.compile_show({}, {}, [], target)}
    )['Evening']
    revision = history.Revision(
        number=3, modified=12345, length=len(setup), label='Take'
    )
    client = SavedPedal(
        setup,
        revision,
        songs.encode(songs.Library(setlists=[songs.Setlist(name='Encore')])),
    )
    for directory in (
        'setups/fonts',
        'setups/movies',
        'setups/midi',
        'history/Evening',
    ):
        (tmp_path / directory).mkdir(parents=True)
    (tmp_path / 'selected.txt').write_text('Evening')
    (tmp_path / 'catalogue.txt').write_text('Evening True')
    for name, field in {
        'setups/Evening.bin': 'setup',
        'setups/fonts/12345678.cfont': 'font',
        'setups/movies/87654321.cnsa': 'movie',
        'setups/midi/take.mid': 'midi',
        'history/Evening/3.bin': 'history',
        'songs.bin': 'songs',
    }.items():
        (tmp_path / name).write_bytes(client.blobs[field])
    (tmp_path / 'history.toml').write_text(
        tomlkit.dumps(
            {
                'revisions': [
                    revision.model_dump()
                    | {
                        'name': 'Evening',
                        'number': '3',
                        'version': compiler_wire.DOCUMENT_VERSION,
                    }
                ]
            }
        )
    )
    monkeypatch.setattr(
        transport, 'SerialConnection', lambda *a, **k: nullcontext(None)
    )
    monkeypatch.setattr(transport, 'SetupClient', lambda connection: client)
    return client


@pytest.mark.parametrize('reference', ['unused', 'setup', 'history'])
def test_unreadable_movies_block_backup_only_when_referenced(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    target: models.Target,
    saved_pedal: SavedPedal,
    reference: str,
) -> None:
    geometry = asset.Geometry(width=1, height=1)
    movie = asset.encode_frames(b'\x00\x00', [100], geometry)
    path = tmp_path / 'movie.cnsa'
    path.write_bytes(movie)
    target = target.model_copy(update={'animation': geometry})
    if reference != 'unused':
        saved_pedal.blobs[reference] = compiler.compile_show(
            {'splash_movie': [str(path)]}, {}, [], target
        )
        saved_pedal.revision = saved_pedal.revision.model_copy(
            update={'length': len(saved_pedal.blobs['history'])}
        )
    name = f'{crc32(movie):08x}.bin'
    entries = iter(
        [
            'CNTRL STORAGE 1000000 900000 - 512',
            'CNTRL ENTRY 1 0 D 0 636e74726c',
            'CNTRL ENTRY 2 1 D 0 6d6f76696573',
            f'CNTRL ENTRY 3 2 F {len(movie)} {name.encode().hex()}',
            'CNTRL STORAGE END 3',
        ]
    )
    original_request = saved_pedal.request

    def request(command: str) -> str:
        return (
            next(entries)
            if command.startswith('CNTRL STORAGE')
            else original_request(command)
        )

    def download_movie(key: int) -> bytes:
        assert key == crc32(movie)
        raise transport.UsbFileMissing('device cannot read the stored movie')

    monkeypatch.setattr(saved_pedal, 'request', request)
    monkeypatch.setattr(saved_pedal, 'download_movie', download_movie)
    directory = tmp_path / 'before'
    if reference != 'unused':
        with pytest.raises(FileNotFoundError, match='cannot read the stored movie'):
            backups.backup_pedal(directory, Path('/unused'))
        with pytest.raises(ValueError, match='interrupted backup'):
            backups.check_conversion(directory)
    else:
        backups.backup_pedal(directory, Path('/unused'))
        backups.check_conversion(directory)
        backups.prepare_restore(directory, tmp_path / 'converted', target)
    assert (
        directory / 'unavailable-assets.txt'
    ).read_text() == f'/cntrl/movies/{name}\n'


@pytest.mark.parametrize('root', [0, 7])
def test_flash_restore_replaces_archived_language_catalogue(
    tmp_path: Path, target: models.Target, root: int
) -> None:
    before = tmp_path / 'before'
    (before / 'setups').mkdir(parents=True)
    (before / 'selected.txt').write_text('')
    (before / 'songs.bin').write_bytes(songs.encode(songs.Library()))
    (before / 'status.txt').write_text(f'CNTRL READY persistent_root={root}')
    (before / 'device-globals.toml').write_text(
        'format = 3\nlanguage = "fr"\n[values]\n'
    )
    destination_pack = languages.pack('fr')
    old_pack = bytearray(destination_pack)
    old_pack[64] ^= 1
    old_pack[12:16] = bytes(4)
    struct.pack_into('<I', old_pack, 12, crc32(old_pack))
    with pytest.raises(catalogue.IncompatibleCatalogueError):
        catalogue.inspect(bytes(old_pack))
    storage = before / 'raw/cntrl'
    if root:
        storage /= f'generations/{root:08x}'
    archived = storage / 'languages/fr.bin'
    archived.parent.mkdir(parents=True)
    archived.write_bytes(old_pack)
    package = tmp_path / 'package'
    package.mkdir()
    (package / 'fr.bin').write_bytes(destination_pack)
    restored = backups.prepare_restore(
        before, tmp_path / 'restore', target, packaged_languages=package
    )
    assert (restored / 'raw/cntrl/languages/fr.bin').read_bytes() == destination_pack
    assert archived.read_bytes() == old_pack
    assert (
        device_globals.load((restored / 'device-globals.toml').read_bytes()).language
        == 'fr'
    )


def test_post_flash_verification_accepts_all_saved_data(
    tmp_path: Path,
    target: models.Target,
    saved_pedal: SavedPedal,
    capsys: pytest.CaptureFixture[str],
) -> None:
    backups.verify_after_flashing(tmp_path, Path('/unused'), target)
    assert (
        'Selection, setups, history, assets and songs verified unchanged.'
        in capsys.readouterr().out
    )


@pytest.mark.parametrize('version', [44, 45])
def test_pre_flash_backup_keeps_previous_development_bytes_and_can_convert_them(
    tmp_path: Path,
    target: models.Target,
    saved_pedal: SavedPedal,
    version: int,
    legacy_setups: dict[int, bytes],
) -> None:
    original = bytearray(binary_format.wrap(legacy_setups[version]))
    struct.pack_into('<H', original, 4, 0x8000 | version)
    struct.pack_into('<I', original, 16, 0)
    struct.pack_into('<I', original, 16, crc32(original))
    saved_pedal.blobs['setup'] = saved_pedal.blobs['history'] = bytes(original)
    saved_pedal.revision = saved_pedal.revision.model_copy(
        update={'length': len(original)}
    )
    saved_pedal.status = f'CNTRL READY {version} 0 00000000'
    directory = tmp_path / 'before'
    backups.backup_pedal(directory, Path('/unused'))
    assert (directory / 'setups/Evening.bin').read_bytes() == original
    assert (directory / 'history/Evening/3.bin').read_bytes() == original
    backups.check_conversion(directory)
    manifest = tomlkit.parse((directory / 'history.toml').read_text()).unwrap()
    assert manifest['revisions'][0]['version'] == version
    converted = backups.prepare_restore(directory, tmp_path / 'converted', target)
    restored = (converted / 'setups/Evening.bin').read_bytes()
    assert binary_format.identify(restored) == 'd46'
    assert resolved.export(restored) == resolved.export(
        resolved.upgrade(bytes(original))
    )


@pytest.mark.parametrize(
    'mismatch,expected',
    [
        ('status', 'expected READY format'),
        ('selection', 'selection differs'),
        ('catalogue', 'setup catalogue differs'),
        ('setup', 'setup binary differs'),
        ('font', 'font differs'),
        ('movie', 'movie differs'),
        ('midi', 'MIDI file differs'),
        ('history-metadata', 'history metadata differs'),
        ('history', 'history differs'),
        ('songs', 'song library differs'),
    ],
)
def test_post_flash_verification_identifies_each_isolated_mismatch(
    tmp_path: Path,
    target: models.Target,
    saved_pedal: SavedPedal,
    mismatch: str,
    expected: str,
) -> None:
    if mismatch == 'status':
        saved_pedal.status = (
            f'CNTRL READY {compiler_wire.DOCUMENT_VERSION - 1} 0 00000000'
        )
    elif mismatch == 'selection':
        saved_pedal.selected = 'Other'
    elif mismatch == 'catalogue':
        saved_pedal.valid = False
    elif mismatch == 'history-metadata':
        saved_pedal.revision = saved_pedal.revision.model_copy(
            update={'label': 'Changed'}
        )
    else:
        saved_pedal.blobs[mismatch] += b'changed'
    with pytest.raises(ValueError, match=expected):
        backups.verify_after_flashing(tmp_path, Path('/unused'), target)
