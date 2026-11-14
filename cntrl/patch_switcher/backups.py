"""Back up pedal data and upgrade supported setup formats without recompiling."""

from __future__ import annotations

import shutil
import struct
import sys
from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel

from . import (
    compiler,
    data_files,
    history,
    midi_assets,
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
    target: Path
    """Current target description, used to refresh RAM estimates."""
    preserve_memory_estimates: bool = False
    """Keep recorded RAM estimates for the same firmware and current data format."""


class Verify(BaseModel, frozen=True):
    """Compare the flashed pedal with a backup without changing saved data."""

    directory: Annotated[Path, tyro.conf.Positional]
    port: Path
    target: Path
    preserve_memory_estimates: bool = False
    """Compare recorded RAM estimates when verifying a same-firmware restore."""


def backup_pedal(
    directory: Path, port: Path, connection: transport.SerialConnection | None = None
) -> None:
    directory.mkdir(parents=True)
    setups = directory / 'setups'
    setups.mkdir()
    print(f'Backup: {directory}', flush=True)
    with connection or transport.SerialConnection(port, timeout=5) as connection:
        client = transport.SetupClient(connection)
        selected = client.current()
        entries = client.catalogue()
        (directory / 'selected.txt').write_bytes(selected.encode())
        (directory / 'catalogue.txt').write_bytes(
            '\n'.join(f'{e.name} {e.valid}' for e in entries).encode()
        )
        status = client.request('CNTRL STATUS')
        (directory / 'status.txt').write_bytes(status.encode())
        fonts: set[int] = set()
        movies: set[int] = set()
        snapshots: list[dict[str, object]] = []
        for entry in entries:
            if entry.valid:
                data = client.download(entry.name)
                (setups / f'{entry.name}.bin').write_bytes(data)
                text = resolved.export(data)
                (setups / f'{entry.name}.toml').write_bytes(text.encode())
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
                (directory / 'history.toml').write_bytes(
                    tomlkit.dumps({'revisions': snapshots}).encode()
                )
                if version in (36, 38, 39, 40, compiler.DOCUMENT_VERSION):
                    (folder / f'{revision.number}.toml').write_bytes(
                        resolved.export(data).encode()
                    )
                    backup_assets(client, data, setups, fonts, movies)
                print(
                    f'Saved {entry.name} revision {revision.number}: {len(data)} bytes',
                    flush=True,
                )
        (directory / 'history.toml').write_bytes(
            tomlkit.dumps({'revisions': snapshots}).encode()
        )
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
    for name in midi_assets.referenced({'setup': data}):
        save_asset(folder / 'midi' / name, client.download_midi(name))


def backup_stored_assets(client: transport.SetupClient, directory: Path) -> None:
    """Save readable stored assets, including those used only by older snapshots."""
    from .cli import save_asset

    first = True

    def next_line() -> str:
        nonlocal first
        command = 'CNTRL STORAGE SCAN' if first else 'CNTRL STORAGE NEXT'
        first = False
        return client.request(command)

    report = storage_info.read_storage_report(next_line)
    (directory / 'storage.toml').write_bytes(
        tomlkit.dumps(report.model_dump(exclude_none=True)).encode()
    )
    unavailable: list[str] = []
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
            try:
                data = client.download_movie(key)
            except FileNotFoundError as error:
                unavailable.append(entry.path)
                print(f'Not backed up: {entry.path}: {error}', file=sys.stderr)
                continue
            save_asset(
                directory / 'setups/movies' / f'{key:08x}.cnsa',
                data,
            )
    if unavailable:
        (directory / 'unavailable-assets.txt').write_bytes(
            ('\n'.join(unavailable) + '\n').encode()
        )


def restore_setups(
    directory: Path,
    port: Path,
    target: models.Target,
    *,
    preserve_memory_estimates: bool = False,
) -> None:
    folder = directory / 'setups'
    selected = (directory / 'selected.txt').read_bytes().decode()
    fonts = {
        int(f.stem, 16): f.read_bytes()
        for f in sorted((folder / 'fonts').glob('*.cfont'))
    }
    setups = {
        n: resolved.prepare_restore(
            d,
            fonts,
            switches=bool(target.max_switches),
            preserve_memory_estimates=preserve_memory_estimates,
        )
        for n, d in read_setup_binaries(directory).items()
    }
    snapshots = [
        (
            n,
            r,
            resolved.prepare_restore(
                d,
                fonts,
                switches=bool(target.max_switches),
                preserve_memory_estimates=preserve_memory_estimates,
            ),
        )
        for n, r, d in read_history_binaries(directory)
    ]
    library = songs.upgrade(
        (directory / 'songs.bin').read_bytes(),
        fonts,
        switches=bool(target.max_switches),
        preserve_memory_estimates=preserve_memory_estimates,
    )
    if selected and selected not in setups:
        raise ValueError(f'{selected}: saved selection is absent from the backup')
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        for key, data in fonts.items():
            client.copy_font(key, data)
        for p in sorted((folder / 'movies').glob('*.cnsa')):
            client.copy_movie(p.read_bytes())
        for p in sorted((folder / 'midi').glob('*')):
            client.copy_midi(p.name, p.read_bytes(), overwrite=True)
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


def verify_after_flashing(
    directory: Path,
    port: Path,
    target: models.Target,
    *,
    preserve_memory_estimates: bool = False,
) -> None:
    selected = (directory / 'selected.txt').read_bytes().decode()
    catalogue = (directory / 'catalogue.txt').read_bytes().decode()
    fonts = {
        int(f.stem, 16): f.read_bytes()
        for f in sorted((directory / 'setups/fonts').glob('*.cfont'))
    }
    setups = {
        n: resolved.prepare_restore(
            d,
            fonts,
            switches=bool(target.max_switches),
            preserve_memory_estimates=preserve_memory_estimates,
        )
        for n, d in read_setup_binaries(directory).items()
    }
    library = songs.upgrade(
        (directory / 'songs.bin').read_bytes(),
        fonts,
        switches=bool(target.max_switches),
        preserve_memory_estimates=preserve_memory_estimates,
    )
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
        for p in sorted((directory / 'setups/midi').glob('*')):
            if client.download_midi(p.name) != p.read_bytes():
                raise ValueError(f'{p.name}: MIDI file differs from the backup')
        listed: dict[str, list[history.Revision]] = {}
        for name, revision, data in read_history_binaries(directory):
            _, expected = history.normalize_revision(
                name,
                resolved.prepare_restore(
                    data,
                    fonts,
                    switches=bool(target.max_switches),
                    preserve_memory_estimates=preserve_memory_estimates,
                ),
            )
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


def prepare_restore(directory: Path, destination: Path, target: models.Target) -> Path:
    """Validate a backup and prepare current-format copies, retaining originals."""
    setups = read_setup_binaries(directory)
    snapshots = read_history_binaries(directory)
    fonts = {
        int(f.stem, 16): f.read_bytes()
        for f in sorted((directory / 'setups/fonts').glob('*.cfont'))
    }
    library = songs.upgrade(
        (directory / 'songs.bin').read_bytes(),
        fonts,
        switches=bool(target.max_switches),
    )
    selected = (directory / 'selected.txt').read_bytes().decode()
    if selected and selected not in setups:
        raise ValueError(f'{selected}: saved selection is absent from the backup')
    upgraded = {
        n: resolved.prepare_restore(d, fonts, switches=bool(target.max_switches))
        for n, d in setups.items()
    }
    revisions = [
        (
            n,
            r,
            resolved.prepare_restore(d, fonts, switches=bool(target.max_switches)),
        )
        for n, r, d in snapshots
    ]
    shutil.copytree(directory, destination)
    (destination / 'songs.bin').write_bytes(library)
    for name, data in upgraded.items():
        (destination / 'setups' / f'{name}.bin').write_bytes(data)
        (destination / 'setups' / f'{name}.toml').write_bytes(
            resolved.export(data).encode()
        )
    manifest: list[dict[str, object]] = []
    for name, revision, data in revisions:
        folder = destination / 'history' / name
        (folder / f'{revision.number}.bin').write_bytes(data)
        (folder / f'{revision.number}.toml').write_bytes(resolved.export(data).encode())
        manifest.append(
            revision.model_dump()
            | {
                'name': name,
                'number': str(revision.number),
                'length': len(data),
                'version': compiler.DOCUMENT_VERSION,
            }
        )
    (destination / 'history.toml').write_bytes(
        tomlkit.dumps({'revisions': manifest}).encode()
    )
    return destination


def read_history_binaries(directory: Path) -> list[tuple[str, history.Revision, bytes]]:
    path = directory / 'history.toml'
    if not path.exists():
        return []
    document = tomlkit.parse(path.read_bytes().decode()).unwrap()
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
