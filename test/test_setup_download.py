"""A download refuses the entire batch when any destination already exists."""

from pathlib import Path

import pytest
import tyro

from cntrl.patch_switcher import cli, compiler


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
    with pytest.raises(compiler.CompilationError, match='--force/-f'):
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
    with pytest.raises(compiler.CompilationError, match='not a directory'):
        cli.publish_download(
            {Path('Show.toml'): b'new', Path('fonts/font.cfont'): b'font'},
            tmp_path,
            force=True,
        )
    assert not (tmp_path / 'Show.toml').exists()
    assert (tmp_path / 'fonts').read_bytes() == b'not a directory'


def test_download_options_default_to_all_setups_and_current_directory() -> None:
    command = tyro.cli(cli.Download, args=['--port', 'device'])
    assert command.names == []
    assert command.output == Path('.')
    assert command.force is False
    command = tyro.cli(
        cli.Download, args=['Show', '-o', 'destination', '-f', '--port', 'device']
    )
    assert command.names == ['Show']
    assert command.output == Path('destination')
    assert command.force is True
