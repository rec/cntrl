"""Back up, restore and verify pedal data without recompiling saved setups."""

import struct
from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel

from . import (
    compiler,
    data_files,
    history,
    models,
    resolved,
    setup_bundle,
    songs,
    storage_info,
    transport,
)


class Backup(BaseModel, frozen=True):
    """Save setups, history, assets, songs and selection to a new directory."""

    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


class Restore(BaseModel, frozen=True):
    """Restore the backup, including songs and history; keep unrelated setups."""

    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


class Verify(BaseModel, frozen=True):
    """Compare the flashed pedal with a backup without changing saved data."""

    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


def backup_pedal(directory: Path, port: Path) -> None:
    directory.mkdir(parents=True)
    setups = directory / 'setups'
    setups.mkdir()
    print(f'Backup: {directory}', flush=True)
    with transport.SerialConnection(port, timeout=5) as connection:
        client = transport.SetupClient(connection)
        selected = client.current()
        entries = client.catalogue()
        (directory / 'selected.txt').write_text(selected)
        (directory / 'catalogue.txt').write_text(
            '\n'.join(f'{e.name} {e.valid}' for e in entries)
        )
        status = client.request('CNTRL STATUS')
        (directory / 'status.txt').write_text(status)
        fonts: set[int] = set()
        movies: set[int] = set()
        snapshots: list[dict[str, object]] = []
        for entry in entries:
            if entry.valid:
                data = client.download(entry.name)
                (setups / f'{entry.name}.bin').write_bytes(data)
                text = resolved.export(data)
                (setups / f'{entry.name}.toml').write_text(text)
                backup_assets(client, data, setups, fonts, movies)
                print(f'Saved {entry.name}: {len(data)} bytes', flush=True)
            else:
                print(f'Skipped invalid setup: {entry.name}', flush=True)
            for revision in history.revisions(client, entry.name):
                data = client.download(entry.name, revision.number)
                if len(data) != revision.length:
                    raise ValueError(
                        f'{entry.name}/{revision.number}: history length differs'
                    )
                folder = directory / 'history' / entry.name
                folder.mkdir(parents=True, exist_ok=True)
                (folder / f'{revision.number}.bin').write_bytes(data)
                version = struct.unpack_from('<H', data, 4)[0]
                snapshots.append(
                    revision.model_dump()
                    | {
                        'name': entry.name,
                        'number': str(revision.number),
                        'version': version,
                    }
                )
                (directory / 'history.toml').write_text(
                    tomlkit.dumps({'revisions': snapshots})
                )
                if version == compiler.DOCUMENT_VERSION:
                    (folder / f'{revision.number}.toml').write_text(
                        resolved.export(data)
                    )
                    backup_assets(client, data, setups, fonts, movies)
                print(
                    f'Saved {entry.name} revision {revision.number}: {len(data)} bytes',
                    flush=True,
                )
        (directory / 'history.toml').write_text(tomlkit.dumps({'revisions': snapshots}))
        backup_stored_assets(client, directory)
        (directory / 'songs.bin').write_bytes(songs.download(client))
    print(f'Selected: {selected}\n{status}\nBackup complete.', flush=True)


def backup_assets(
    client: transport.SetupClient,
    data: bytes,
    folder: Path,
    fonts: set[int],
    movies: set[int],
) -> None:
    from .cli import save_asset

    settings = models.Setup.model_validate(
        tomlkit.parse(resolved.export(data)).unwrap()
    )
    for value in settings.fonts.values():
        key = int(Path(value).stem, 16)
        if key not in fonts:
            save_asset(folder / 'fonts' / f'{key:08x}.cfont', client.download_font(key))
            fonts.add(key)
    for value in settings.splash_movie + settings.idle_movie:
        key = int(Path(value).stem, 16)
        if key not in movies:
            save_asset(folder / value, client.download_movie(key))
            movies.add(key)


def backup_stored_assets(client: transport.SetupClient, directory: Path) -> None:
    """Save every stored asset, including assets used only by older snapshots."""
    from .cli import save_asset

    first = True

    def next_line() -> str:
        nonlocal first
        command = 'CNTRL STORAGE SCAN' if first else 'CNTRL STORAGE NEXT'
        first = False
        return client.request(command)

    report = storage_info.read_storage_report(next_line)
    (directory / 'storage.toml').write_text(
        tomlkit.dumps(report.model_dump(exclude_none=True))
    )
    for entry in report.entries:
        path = Path(entry.path)
        if (
            entry.directory
            or len(path.stem) != 8
            or any(c not in '0123456789abcdefABCDEF' for c in path.stem)
        ):
            continue
        key = int(path.stem, 16)
        if (
            path.parent.as_posix().lower() == '/cntrl/fonts'
            and path.suffix.lower() == '.cfont'
        ):
            save_asset(
                directory / 'setups/fonts' / f'{key:08x}.cfont',
                client.download_font(key),
            )
        elif (
            path.parent.as_posix().lower() == '/cntrl/movies'
            and path.suffix.lower() == '.bin'
        ):
            save_asset(
                directory / 'setups/movies' / f'{key:08x}.cnsa',
                client.download_movie(key),
            )


def restore_setups(directory: Path, port: Path) -> None:
    folder = directory / 'setups'
    selected = (directory / 'selected.txt').read_text()
    setups = read_setup_binaries(directory)
    snapshots = read_history_binaries(directory)
    library = (directory / 'songs.bin').read_bytes()
    songs.decode(library)
    if selected and selected not in setups:
        raise ValueError(f'{selected}: saved selection is absent from the backup')
    fonts = {
        int(f.stem, 16): f.read_bytes()
        for f in sorted((folder / 'fonts').glob('*.cfont'))
    }
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        for key, data in fonts.items():
            client.copy_font(key, data)
        for p in sorted((folder / 'movies').glob('*.cnsa')):
            client.copy_movie(p.read_bytes())
        current_bundle = client.download_bundle()
        existing = setup_bundle.unpack(current_bundle) if current_bundle else {}
        combined = setup_bundle.reindex(existing | setups)
        if combined:
            source: dict[str, object] = {
                n: tomlkit.parse(resolved.export(d)).unwrap()
                for n, d in combined.items()
            }
            margin = (
                setup_bundle.HEADER.unpack_from(current_bundle)[5]
                if current_bundle
                else 16192
            )
            bundle = setup_bundle.build(
                combined,
                source,
                setup_bundle.memory_from_bundle(current_bundle)
                if current_bundle
                else client.memory(),
                margin,
            )
            client.copy_bundle(bundle.pack(), overwrite=True)
        for name, revision, data in snapshots:
            history.import_revision(client, name, revision, data)
            print(
                f'{name} revision {revision.number}: restored and read-back verified',
                flush=True,
            )
        for name, number in history.reserved_numbers(directory):
            encoded = data_files.validate_name(name).encode('ascii').hex()
            response = client.request(f'CNTRL HISTORY RESERVE {encoded} {number}')
            if response not in ('CNTRL OK', 'CNTRL EXISTS'):
                raise ValueError(
                    f'{name}/{number}: could not restore history reservation'
                )
        songs.upload(client, library, overwrite=True)
        for name in setups:
            print(f'{name}: restored and read-back verified', flush=True)
        if setups:
            client.ok('CNTRL SETUP FINISH')
        if selected:
            client.select(selected)
            if client.current() != selected:
                raise ValueError('restored selection does not match the backup')
            print(f'Selected: {selected}', flush=True)


def verify_after_flashing(directory: Path, port: Path) -> None:
    setups = read_setup_binaries(directory)
    selected = (directory / 'selected.txt').read_text()
    catalogue = (directory / 'catalogue.txt').read_text()
    library = (directory / 'songs.bin').read_bytes()
    fonts = {
        int(f.stem, 16): f.read_bytes()
        for f in sorted((directory / 'setups/fonts').glob('*.cfont'))
    }
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        status = client.request('CNTRL STATUS')
        parts = status.split()
        if len(parts) < 5 or parts[:3] != [
            'CNTRL',
            'READY',
            str(compiler.DOCUMENT_VERSION),
        ]:
            raise ValueError(
                f'expected READY format {compiler.DOCUMENT_VERSION}; received {status}'
            )
        actual = client.current()
        if actual != selected:
            raise ValueError(
                f'selection differs: expected {selected!r}, received {actual!r}'
            )
        actual_catalogue = '\n'.join(f'{e.name} {e.valid}' for e in client.catalogue())
        if actual_catalogue != catalogue:
            raise ValueError(
                f'setup catalogue differs:\nExpected:\n{catalogue}\n'
                f'Received:\n{actual_catalogue}'
            )
        for name, data in setups.items():
            if client.download(name) != data:
                raise ValueError(f'{name}: setup binary differs from the backup')
            print(f'{name}: verified identical', flush=True)
        for key, data in fonts.items():
            if client.download_font(key) != data:
                raise ValueError(f'{key:08x}: font differs from the backup')
        for p in sorted((directory / 'setups/movies').glob('*.cnsa')):
            if client.download_movie(int(p.stem, 16)) != p.read_bytes():
                raise ValueError(f'{p.name}: movie differs from the backup')
        listed: dict[str, list[history.Revision]] = {}
        for name, revision, data in read_history_binaries(directory):
            _, expected = history.normalize_revision(name, data)
            if name not in listed:
                listed[name] = history.revisions(client, name)
            actual_revision = next(
                (r for r in listed[name] if r.number == revision.number), None
            )
            if actual_revision != revision.model_copy(update={'length': len(expected)}):
                raise ValueError(
                    f'{name}/{revision.number}: '
                    'history metadata differs from the backup'
                )
            if client.download(name, revision.number) != expected:
                raise ValueError(
                    f'{name}/{revision.number}: history differs from the backup'
                )
        if songs.download(client) != library:
            raise ValueError('song library differs from the backup')
    print(
        f'{status}\nSelection, setups, history, assets and songs verified unchanged.',
        flush=True,
    )


def read_setup_binaries(directory: Path) -> dict[str, bytes]:
    folder = directory / 'setups'
    if not folder.is_dir():
        raise ValueError(f'{folder}: missing backup setup directory')
    setups = {s.stem: s.read_bytes() for s in sorted(folder.glob('*.bin'))}
    for data in setups.values():
        resolved.graph_offset(data)
    return setups


def read_history_binaries(directory: Path) -> list[tuple[str, history.Revision, bytes]]:
    path = directory / 'history.toml'
    if not path.exists():
        return []
    document = tomlkit.parse(path.read_text()).unwrap()
    snapshots = document.get('revisions', [])
    if not isinstance(snapshots, list):
        raise ValueError('invalid history backup manifest')
    result = []
    for entry in snapshots:
        if not isinstance(entry, dict):
            raise ValueError('invalid history backup entry')
        name = data_files.validate_name(str(entry['name']))
        revision = history.Revision.model_validate(entry)
        data = (directory / 'history' / name / f'{revision.number}.bin').read_bytes()
        if len(data) != revision.length:
            raise ValueError(f'{name}/{revision.number}: history backup length differs')
        resolved.graph_offset(data)
        result.append((name, revision, data))
    return result
