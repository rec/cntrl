"""Backup binaries stay authoritative and must match the current format."""

from pathlib import Path

import pytest
import tyro

from cntrl.patch_switcher import backups, cli, compiler


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
    data[4:6] = (compiler.DOCUMENT_VERSION - 1).to_bytes(2, 'little')
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
