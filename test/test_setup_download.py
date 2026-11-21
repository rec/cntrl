"""A download refuses the entire batch when any destination already exists."""

import sys
from contextlib import nullcontext
from pathlib import Path
from unittest.mock import Mock

import pytest
import tyro

from cntrl.patch_chooser import (
    cli,
    compiler_errors,
    device_globals,
    global_overlay,
    resolved,
    transport,
)


@pytest.mark.parametrize(
    'existing',
    ['Show.toml', 'fonts/12345678.cfont', 'movies/12345678.cnsa', 'song.mid'],
)
def test_any_existing_file_prevents_all_writes(tmp_path: Path, existing: str) -> None:
    files = {
        Path('Show.toml'): b'brightness=100\n',
        Path('fonts/12345678.cfont'): b'font',
        Path('movies/12345678.cnsa'): b'movie',
        Path('song.mid'): b'MIDI',
    }
    destination = tmp_path / existing
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Matching bytes are still an existing file and require explicit force.
    destination.write_bytes(files[Path(existing)])
    before = sorted(str(p.relative_to(tmp_path)) for p in tmp_path.rglob('*'))
    with pytest.raises(compiler_errors.CompilationError, match='--force/-f'):
        cli.publish_download(files, tmp_path)
    assert sorted(str(p.relative_to(tmp_path)) for p in tmp_path.rglob('*')) == before
    assert destination.read_bytes() == files[Path(existing)]


def test_force_replaces_batch_files_and_preserves_unrelated_files(
    tmp_path: Path,
) -> None:
    source = tmp_path / 'Show.toml'
    source.write_bytes(b'old')
    unrelated = tmp_path / 'keep.txt'
    unrelated.write_bytes(b'keep')
    cli.publish_download(
        {
            Path('Show.toml'): '# Café\nbrightness=25\n'.encode(),
            Path('song.mid'): b'MIDI',
        },
        tmp_path,
        force=True,
    )
    assert source.read_bytes() == '# Café\nbrightness=25\n'.encode()
    assert (tmp_path / 'song.mid').read_bytes() == b'MIDI'
    assert unrelated.read_bytes() == b'keep'
    assert sorted(p.name for p in tmp_path.iterdir()) == [
        'Show.toml',
        'keep.txt',
        'song.mid',
    ]


def test_unwritable_directory_shape_is_detected_before_any_file_is_written(
    tmp_path: Path,
) -> None:
    (tmp_path / 'fonts').write_bytes(b'not a directory')
    with pytest.raises(compiler_errors.CompilationError, match='not a directory'):
        cli.publish_download(
            {Path('Show.toml'): b'new', Path('fonts/font.cfont'): b'font'},
            tmp_path,
            force=True,
        )
    assert not (tmp_path / 'Show.toml').exists()
    assert (tmp_path / 'fonts').read_bytes() == b'not a directory'


def test_download_options_default_to_all_setups_and_current_directory() -> None:
    command = tyro.cli(cli.SetupDownload, args=['--port', 'device'])
    assert command.names == []
    assert command.output == Path('.')
    assert command.force is False
    command = tyro.cli(
        cli.SetupDownload, args=['Show', '-o', 'destination', '-f', '--port', 'device']
    )
    assert command.names == ['Show']
    assert command.output == Path('destination')
    assert command.force is True


def test_output_failure_names_destination_and_removes_temporary_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    failure = PermissionError('cannot publish download')
    monkeypatch.setattr(Path, 'hardlink_to', Mock(side_effect=failure))
    with pytest.raises(
        cli.DownloadOutputError, match='Show.toml: cannot publish download'
    ) as caught:
        cli.publish_download({Path('Show.toml'): b'new'}, tmp_path)
    assert caught.value.__cause__ is failure
    assert list(tmp_path.iterdir()) == []


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_download_cli_preserves_unexpected_failures(
    monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected download failure')
    client = Mock()
    client.catalogue.return_value = [
        transport.SetupEntry(name='Show', modified=0, length=116, checksum=0)
    ]
    monkeypatch.setattr(transport, 'SerialConnection', Mock(return_value=nullcontext()))
    monkeypatch.setattr(transport, 'SetupClient', Mock(return_value=client))
    monkeypatch.setattr(cli, 'download_files', Mock(side_effect=failure))
    monkeypatch.setattr(
        sys, 'argv', ['cntrl', 'setup', 'download', 'Show', '--port', '/unused']
    )
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure


def test_language_fallback_requires_the_pedal_to_report_a_missing_pack(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    client = Mock()
    client.catalogue.return_value = []
    monkeypatch.setattr(
        device_globals,
        'capture',
        Mock(return_value=global_overlay.Overlay(language='fr')),
    )
    client.download_language.side_effect = transport.UsbFileMissing('pack absent')
    assert set(cli.download_files(client, [])) == {Path('device-globals.toml')}

    failure = FileNotFoundError('unexpected host file missing')
    client.download_language.side_effect = failure
    with pytest.raises(FileNotFoundError) as caught:
        cli.download_files(client, [])
    assert caught.value is failure


def test_download_distinguishes_damaged_setups_from_decoder_failures(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    client = Mock(spec=transport.SetupClient)
    client.catalogue.return_value = [
        transport.SetupEntry(name='Show', modified=0, length=116, checksum=0)
    ]
    client.download.return_value = b'truncated'
    with pytest.raises(transport.UsbReplyError, match='Show: cannot decode setup'):
        cli.download_files(client, ['Show'])

    failure = ValueError('unexpected setup decoder failure')
    monkeypatch.setattr(resolved, 'export', Mock(side_effect=failure))
    with pytest.raises(ValueError) as caught:
        cli.download_files(client, ['Show'])
    assert caught.value is failure
