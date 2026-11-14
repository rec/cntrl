"""Back up pedal data and upgrade supported setup formats without recompiling."""

from __future__ import annotations

import shutil
import struct
import sys
from contextlib import nullcontext
from hashlib import sha256
from pathlib import Path
from typing import Annotated
from uuid import uuid4

import tomlkit
import tyro
from pydantic import BaseModel, Field
from tomlkit.exceptions import ParseError

from . import (
    binary_format,
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


class Convert(BaseModel, frozen=True):
    """Convert a backup offline into a new directory, preserving the original."""

    directory: Annotated[Path, tyro.conf.Positional]
    destination: Path
    target: Path


class Recover(BaseModel, frozen=True):
    """Select a retained, firmware-compatible storage root after a failed update."""

    port: Path
    root: int = Field(ge=0, le=0xFFFFFFFF)


class ConvertedFile(BaseModel, frozen=True):
    path: str
    source_format: str
    destination_format: str
    source_sha256: str
    destination_sha256: str


class Conversion(BaseModel, frozen=True):
    format: str
    complete: bool
    files: list[ConvertedFile]


class SavedFile(BaseModel, frozen=True):
    path: str
    sha256: str


class BackupManifest(BaseModel, frozen=True):
    complete: bool
    raw_state: bool
    files: list[SavedFile]


def check_conversion(directory: Path, *, raw_recovery: bool = False) -> None:
    if (directory / 'backup-pending.toml').exists():
        raise ValueError(f'{directory}: interrupted backup')
    if (directory / 'conversion-pending.toml').exists():
        raise ValueError(
            f'{directory}: interrupted conversion; retain the original backup'
        )
    report = directory / 'conversion.toml'
    if not report.exists():
        report = directory / 'backup.toml'
        if raw_recovery and not report.exists():
            raise ValueError(f'{directory}: missing raw recovery backup manifest')
        if report.exists():
            backup = BackupManifest.model_validate(
                tomlkit.parse(report.read_text()).unwrap()
            )
            if not backup.complete and not (raw_recovery and backup.raw_state):
                raise ValueError(
                    f'{directory}: incomplete backup; retain raw files for recovery'
                )
            for record in backup.files:
                path = (directory / record.path).resolve()
                if (
                    not path.is_relative_to(directory.resolve())
                    or not path.is_file()
                    or sha256(path.read_bytes()).hexdigest() != record.sha256
                ):
                    raise ValueError(f'{record.path}: backup file checksum differs')
        return
    conversion = Conversion.model_validate(tomlkit.parse(report.read_text()).unwrap())
    if not conversion.complete:
        raise ValueError(f'{directory}: incomplete conversion')
    for record in conversion.files:
        path = (directory / record.path).resolve()
        if not path.is_relative_to(directory.resolve()) or not path.is_file():
            raise ValueError(f'{record.path}: converted file missing or outside backup')
        if sha256(path.read_bytes()).hexdigest() != record.destination_sha256:
            raise ValueError(f'{record.path}: converted file checksum differs')


def recording_high_water(directory: Path) -> int:
    """Retain recording-name uniqueness without restoring a stale counter sector."""
    minimum = 0
    counter = directory / 'starts.toml'
    if counter.exists():
        data = counter.read_bytes()
        valid = False
        for i in range(2):
            record = data[i * 512 : (i + 1) * 512]
            if len(record) != 512:
                continue
            try:
                values = tomlkit.parse(record.decode('ascii')).unwrap()
            except (UnicodeDecodeError, ParseError):
                continue
            value = values.get(f'slot{i}')
            if not isinstance(value, dict):
                continue
            count, check = value.get('starts'), value.get('check')
            if (
                type(count) is int
                and 0 < count <= 0x7FFFFFFFFFFFFFFF
                and isinstance(check, str)
                and check.lower() == f'{count ^ 0xFFFFFFFFFFFFFFFF:016x}'
            ):
                minimum = max(minimum, count)
                valid = True
        if not valid:
            raise ValueError(f'{counter}: no valid startup counter record')
    for path in (directory / 'setups/midi').glob('*.mid'):
        parts = path.stem.split('-')
        if len(parts) == 2 and all(p.isascii() and p.isdigit() for p in parts):
            count = int(parts[0])
            if 0 < count <= 0x7FFFFFFFFFFFFFFF:
                minimum = max(minimum, count)
    return minimum


def artifact_format(path: Path) -> str:
    if path.suffix == '.toml':
        return 'TOML'
    if path.suffix == '.txt':
        return 'text'
    with path.open('rb') as source:
        return binary_format.identify(source.read(binary_format.HEADER.size))


def backup_pedal(
    directory: Path, port: Path, connection: transport.SerialConnection | None = None
) -> None:
    from .setup_sync import write_record

    directory.mkdir(parents=True)
    write_record(directory / 'backup-pending.toml', {'complete': False})
    setups = directory / 'setups'
    setups.mkdir()
    print(f'Backup: {directory}', flush=True)
    with (
        nullcontext(connection)
        if connection is not None
        else transport.SerialConnection(port, timeout=5) as connection
    ):
        client = transport.SetupClient(connection)
        status = client.request('CNTRL STATUS')
        recovery_only = status.startswith('CNTRL ERROR')
        if recovery_only:
            deployment = client.request('CNTRL DEPLOY STATUS').split()
            if len(deployment) != 5 or deployment[:2] != ['CNTRL', 'DEPLOY']:
                raise ValueError(f'{status}; raw recovery protocol is unavailable')
            status += f' persistent_format=unknown persistent_root={int(deployment[2])}'
        (directory / 'status.txt').write_bytes(status.encode())
        if ' starts=' in status:
            (directory / 'starts.toml').write_bytes(client.download_starts())
        backup_stored_assets(client, directory)
        raw_state = backup_state(client, directory, status)
        if recovery_only:
            write_backup_manifest(directory, complete=False, raw_state=raw_state)
            print(
                'Raw recovery backup retained; active data is unreadable.', flush=True
            )
            return
        selected = client.current()
        entries = client.catalogue()
        (directory / 'selected.txt').write_bytes(selected.encode())
        (directory / 'catalogue.txt').write_bytes(
            '\n'.join(f'{e.name} {e.valid}' for e in entries).encode()
        )
        fonts = {int(p.stem, 16) for p in (setups / 'fonts').glob('*.cfont')}
        movies = {int(p.stem, 16) for p in (setups / 'movies').glob('*.cnsa')}
        healthy = all(e.valid for e in entries)
        snapshots: list[dict[str, object]] = []
        current_entries = {e.name: e for e in entries}
        history_names = {n for n, _ in history.reserved_numbers(directory)}
        for name in sorted(current_entries.keys() | history_names, key=str.lower):
            entry = current_entries.get(name)
            if entry is not None and entry.valid:
                data = client.download(name)
                (setups / f'{name}.bin').write_bytes(data)
                try:
                    text = resolved.export(data)
                    (setups / f'{name}.toml').write_bytes(text.encode())
                    backup_assets(client, data, setups, fonts, movies)
                except ValueError as error:
                    healthy = False
                    print(f'{name}: retained raw setup: {error}', file=sys.stderr)
                print(f'Saved {name}: {len(data)} bytes', flush=True)
            elif entry is not None:
                print(f'Skipped invalid setup: {name}', flush=True)
            for revision in history.revisions(client, name):
                data = client.download(name, revision.number)
                if len(data) != revision.length:
                    raise ValueError(
                        f'{name}/{revision.number}: history length differs'
                    )
                folder = directory / 'history' / name
                folder.mkdir(parents=True, exist_ok=True)
                (folder / f'{revision.number}.bin').write_bytes(data)
                try:
                    version = struct.unpack_from('<H', binary_format.core(data), 4)[0]
                except ValueError:
                    version = struct.unpack_from('<H', data, 4)[0]
                    healthy = False
                snapshots.append(
                    revision.model_dump()
                    | {
                        'name': name,
                        'number': str(revision.number),
                        'version': version,
                    }
                )
                (directory / 'history.toml').write_bytes(
                    tomlkit.dumps({'revisions': snapshots}).encode()
                )
                if version in binary_format.SETUP_VERSIONS:
                    (folder / f'{revision.number}.toml').write_bytes(
                        resolved.export(data).encode()
                    )
                    backup_assets(client, data, setups, fonts, movies)
                print(
                    f'Saved {name} revision {revision.number}: {len(data)} bytes',
                    flush=True,
                )
        (directory / 'history.toml').write_bytes(
            tomlkit.dumps({'revisions': snapshots}).encode()
        )
        (directory / 'songs.bin').write_bytes(songs.download(client))
        if raw_state and (bundle := client.download_bundle()):
            (directory / 'bundle.bin').write_bytes(bundle)
        if raw_state and (undo := songs.download(client, undo=True)):
            (directory / 'songs-undo.bin').write_bytes(undo)
        complete = healthy and not (directory / 'unavailable-assets.txt').exists()
        write_backup_manifest(directory, complete=complete, raw_state=raw_state)
    result = 'complete' if complete else 'incomplete; raw recovery data retained'
    print(f'Selected: {selected}\n{status}\nBackup {result}.', flush=True)


def write_backup_manifest(directory: Path, *, complete: bool, raw_state: bool) -> None:
    from .setup_sync import write_record

    manifest = BackupManifest(
        complete=complete,
        raw_state=raw_state,
        files=[
            SavedFile(
                path=p.relative_to(directory).as_posix(),
                sha256=sha256(p.read_bytes()).hexdigest(),
            )
            for p in sorted(directory.rglob('*'))
            if p.is_file() and p.name not in ('backup-pending.toml', 'backup.toml')
        ],
    )
    write_record(directory / 'backup.toml', manifest.model_dump())
    (directory / 'backup-pending.toml').unlink()


def backup_state(client: transport.SetupClient, directory: Path, status: str) -> bool:
    """Preserve active journals and adapter files without interpreting their bytes."""
    if ' persistent_format=' not in status:
        return False
    report = storage_info.StorageInfo.model_validate(
        tomlkit.parse((directory / 'storage.toml').read_text()).unwrap()
    )
    root = next(
        (
            int(v.split('=', 1)[1])
            for v in status.split()
            if v.startswith('persistent_root=')
        ),
        0,
    )
    prefix = f'/cntrl/.generations/{root:08x}/' if root else ''
    for entry in report.entries:
        if entry.directory or not entry.path.startswith('/cntrl/'):
            continue
        if '/.generations/' in entry.path and not entry.path.startswith(prefix):
            continue
        logical = (
            '/cntrl/' + entry.path[len(prefix) :]
            if prefix and entry.path.startswith(prefix)
            else entry.path
        )
        if (
            root
            and logical == entry.path
            and logical.startswith(
                (
                    '/cntrl/setups/',
                    '/cntrl/history/',
                    '/cntrl/songs/',
                    '/cntrl/selection/',
                )
            )
        ):
            continue
        asset = Path(logical)
        if (
            asset.parent.as_posix() == '/cntrl/fonts'
            and (directory / 'setups/fonts' / asset.name).exists()
        ):
            continue
        if (
            asset.parent.as_posix() == '/cntrl/movies'
            and (directory / 'setups/movies' / f'{asset.stem}.cnsa').exists()
        ):
            continue
        if (
            asset.parent.as_posix() == '/cntrl/midi'
            and (directory / 'setups/midi' / asset.name).exists()
        ):
            continue
        path = directory / 'raw' / entry.path.lstrip('/')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(client.download_state(entry.path))
    return True


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
        if not (folder / 'midi' / name).exists():
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
            not entry.directory
            and path.parent.as_posix().lower() == '/cntrl/midi'
            and path.suffix.lower() == '.mid'
        ):
            try:
                data = client.download_midi(path.name)
            except (OSError, ValueError) as error:
                unavailable.append(entry.path)
                print(f'Not backed up: {entry.path}: {error}', file=sys.stderr)
                continue
            save_asset(directory / 'setups/midi' / path.name, data)
            continue
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
            try:
                data = client.download_font(key)
            except (OSError, ValueError) as error:
                unavailable.append(entry.path)
                print(f'Not backed up: {entry.path}: {error}', file=sys.stderr)
                continue
            save_asset(directory / 'setups/fonts' / f'{key:08x}.cfont', data)
        elif (
            path.parent.as_posix().lower() == '/cntrl/movies'
            and path.suffix.lower() == '.bin'
        ):
            try:
                data = client.download_movie(key)
            except (OSError, ValueError) as error:
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
    check_conversion(directory)
    minimum_starts = recording_high_water(directory)
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
    undo_path = directory / 'songs-undo.bin'
    undo = (
        songs.upgrade(
            undo_path.read_bytes(),
            fonts,
            switches=bool(target.max_switches),
            preserve_memory_estimates=preserve_memory_estimates,
        )
        if undo_path.exists()
        else b''
    )
    validate_assets(
        directory, [*setups.values(), *(d for _, _, d in snapshots)], [library, undo]
    )
    if selected and selected not in setups:
        raise ValueError(f'{selected}: saved selection is absent from the backup')
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        active_ready = client.recover_deployment()
        if not active_ready and not (directory / 'conversion.toml').exists():
            raise ValueError(
                'recovery installation requires an offline converted backup'
            )
        recovery = (
            directory.parent / f'{directory.name}-before-restore-{uuid4().hex[:12]}'
        )
        backup_pedal(recovery, port, connection)
        check_conversion(recovery, raw_recovery=not active_ready)
        previous = {
            (n, r.number): (n, r, binary_format.wrap(resolved.upgrade(d)))
            for n, r, d in (read_history_binaries(recovery) if active_ready else [])
        }
        for name, revision, data in snapshots:
            key = (name, revision.number)
            if key in previous:
                _, old, original = previous[key]
                if (
                    old.label != revision.label
                    or old.modified != revision.modified
                    or resolved.export(original) != resolved.export(data)
                ):
                    raise ValueError(
                        f'{name}/{revision.number}: history conflicts with device; '
                        f'backup: {recovery}'
                    )
            previous[key] = (name, revision, data)
        snapshots = list(previous.values())
        current_bundle = client.download_bundle() if active_ready else b''
        existing = setup_bundle.unpack(current_bundle) if current_bundle else {}
        combined = setup_bundle.reindex(existing | setups)
        template_path = directory / 'bundle.bin'
        template = (
            template_path.read_bytes() if template_path.exists() else current_bundle
        )
        if (
            current_bundle
            and template_path.exists()
            and existing.keys() - setups.keys()
        ):
            old_metadata = (
                binary_format.unpack(current_bundle, binary_format.Kind.bundle).records[
                    1:
                ]
                if current_bundle.startswith(binary_format.MAGIC)
                else []
            )
            new_metadata = (
                binary_format.unpack(template, binary_format.Kind.bundle).records[1:]
                if template.startswith(binary_format.MAGIC)
                else []
            )
            if old_metadata != new_metadata:
                raise ValueError(
                    'cannot merge different opaque bundle metadata; '
                    f'original retained in {recovery}'
                )
        library = songs.reindex(library, list(combined))
        if undo:
            undo = songs.reindex(undo, list(combined))
        bundle_data = b''
        if not combined and template_path.exists():
            bundle_data = template
            if setup_bundle.unpack(bundle_data):
                raise ValueError(
                    'backup bundle contains setups absent from its setup files'
                )
        if combined:
            source: dict[str, object] = {
                n: tomlkit.parse(resolved.export(d)).unwrap()
                for n, d in combined.items()
            }
            margin = (
                setup_bundle.HEADER.unpack_from(
                    binary_format.core(current_bundle, binary_format.Kind.bundle)
                )[5]
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
                previous=template or None,
            )
            bundle_data = bundle.pack()
        reservations = sorted(
            set(history.reserved_numbers(directory))
            | set(history.reserved_numbers(recovery))
        )
        report_path = recovery / 'storage.toml'
        if report_path.exists():
            report = storage_info.StorageInfo.model_validate(
                tomlkit.parse(report_path.read_text()).unwrap()
            )
            lengths = [len(bundle_data) + 100] * 2
            lengths.extend([len(library) + 28, len(library) + 28, len(undo) + 28])
            lengths.extend(len(history.pack_revision(n, r, d)) for n, r, d in snapshots)
            stored = {e.path for e in report.entries if not e.directory}
            for kind, pattern in (
                ('fonts', '*.cfont'),
                ('movies', '*.cnsa'),
                ('midi', '*'),
            ):
                for path in (folder / kind).glob(pattern):
                    name = f'{path.stem}.bin' if kind == 'movies' else path.name
                    if f'/cntrl/{kind}/{name}' not in stored:
                        lengths.append(path.stat().st_size)
            # Includes both activation slots, selection journals, directory
            # clusters, and a replacement/upload slot while journals advance.
            directories = 16 + len(combined) * 2 + len(reservations) + len(snapshots)
            check_deployment_space(report, lengths + [96, 96, 24, 24], directories)
        for key, data in fonts.items():
            client.copy_font(key, data)
        for p in sorted((folder / 'movies').glob('*.cnsa')):
            client.copy_movie(p.read_bytes())
        for p in sorted((folder / 'midi').glob('*')):
            client.copy_midi(p.name, p.read_bytes(), overwrite=False)
        if minimum_starts:
            client.ok(f'CNTRL STARTS ENSURE {minimum_starts}')
        if active_ready:
            client.begin_deployment()
        else:
            print(
                'Installing converted collection; original collection retained in '
                f'{recovery} and its device root.',
                flush=True,
            )
            client.begin_deployment(recovery=True)
        if bundle_data:
            client.copy_bundle(bundle_data, overwrite=True)
        for name, revision, data in snapshots:
            history.import_revision(client, name, revision, data)
            print(
                f'{name} revision {revision.number}: restored and read-back verified',
                flush=True,
            )
        for name, number in reservations:
            encoded = data_files.validate_name(name).encode('ascii').hex()
            response = client.request(f'CNTRL HISTORY RESERVE {encoded} {number}')
            if response not in ('CNTRL OK', 'CNTRL EXISTS'):
                raise ValueError(
                    f'{name}/{number}: could not restore history reservation'
                )
        if undo:
            songs.upload(client, undo, overwrite=True)
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
        client.commit_deployment(bundle_data, library)
        print(f'Deployment committed. Recovery backup: {recovery}', flush=True)


def verify_after_flashing(
    directory: Path,
    port: Path,
    target: models.Target,
    *,
    preserve_memory_estimates: bool = False,
) -> None:
    check_conversion(directory)
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
            if binary_format.wrap(client.download(name)) != binary_format.wrap(data):
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
            actual_data = client.download(name, revision.number)
            if binary_format.wrap(actual_data) != binary_format.wrap(expected):
                raise ValueError(
                    f'{name}/{revision.number}: history differs from the backup'
                )
            if actual_revision != revision.model_copy(
                update={'length': len(actual_data)}
            ):
                raise ValueError(
                    f'{name}/{revision.number}: '
                    'history metadata differs from the backup'
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
    for name, data in setups.items():
        try:
            resolved.graph_offset(data)
        except ValueError as error:
            raise ValueError(f'{folder / name}: {error}') from error
    return setups


def validate_assets(
    directory: Path, setups: list[bytes], libraries: list[bytes]
) -> None:
    """Require every executable document's assets before deployment or conversion."""
    needed: set[str] = set()
    for library in libraries:
        if library:
            needed.update(
                p for v in songs.asset_references(library).values() for p in v
            )
    for data in setups:
        settings = models.Setup.model_validate(
            tomlkit.parse(resolved.export(data)).unwrap()
        )
        needed.update(settings.fonts.values())
        needed.update(settings.splash_movie + settings.idle_movie)
        needed.update(f'midi/{n}' for n in midi_assets.referenced({'setup': data}))
    asset_root = (directory / 'setups').resolve()
    for name in sorted(needed):
        path = (asset_root / name).resolve()
        if not path.is_relative_to(asset_root) or not path.is_file():
            raise ValueError(f'{name}: required asset is absent from the backup')


def check_deployment_space(
    report: storage_info.StorageInfo, lengths: list[int], directories: int
) -> None:
    """Budget staging without counting retained committed files as reclaimable."""
    if report.free_bytes is None:
        raise ValueError('deployment requires a storage scan with free space')
    unit = report.allocation_unit_bytes
    required = (directories + sum((n + unit - 1) // unit for n in lengths)) * unit
    if required > report.free_bytes:
        raise ValueError(
            f'deployment needs {required} free bytes including staging; '
            f'only {report.free_bytes} are available'
        )


def prepare_restore(directory: Path, destination: Path, target: models.Target) -> Path:
    """Validate a backup and prepare current-format copies, retaining originals."""
    from .setup_sync import write_record

    check_conversion(directory)
    recording_high_water(directory)
    if destination.resolve().is_relative_to(directory.resolve()):
        raise ValueError('conversion destination must be outside the original backup')
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
    undo_path = directory / 'songs-undo.bin'
    undo = (
        songs.upgrade(undo_path.read_bytes(), fonts, switches=bool(target.max_switches))
        if undo_path.exists()
        else b''
    )
    selected = (directory / 'selected.txt').read_bytes().decode()
    if selected and selected not in setups:
        raise ValueError(f'{selected}: saved selection is absent from the backup')
    upgraded = {
        n: binary_format.wrap(
            resolved.prepare_restore(d, fonts, switches=bool(target.max_switches))
        )
        for n, d in setups.items()
    }
    revisions = [
        (
            n,
            r,
            binary_format.wrap(
                resolved.prepare_restore(d, fonts, switches=bool(target.max_switches))
            ),
        )
        for n, r, d in snapshots
    ]
    validate_assets(
        directory, [*upgraded.values(), *(d for _, _, d in revisions)], [library, undo]
    )
    bundle = b''
    bundle_path = directory / 'bundle.bin'
    if bundle_path.exists():
        original_bundle = bundle_path.read_bytes()
        if setup_bundle.unpack(original_bundle) != setups:
            raise ValueError(f'{bundle_path}: bundle and authoritative setups differ')
        core = binary_format.core(original_bundle, binary_format.Kind.bundle)
        if upgraded:
            bundle = setup_bundle.build(
                upgraded,
                {
                    n: tomlkit.parse(resolved.export(d)).unwrap()
                    for n, d in upgraded.items()
                },
                target.memory,
                setup_bundle.HEADER.unpack_from(core)[5],
                previous=original_bundle,
            ).pack()
        else:
            count = setup_bundle.HEADER.unpack_from(core)[2]
            names = [
                setup_bundle.ENTRY.unpack_from(
                    core, setup_bundle.HEADER.size + i * setup_bundle.ENTRY.size
                )[0]
                .split(b'\0')[0]
                .decode('ascii')
                for i in range(count)
            ]
            metadata = (
                binary_format.unpack(
                    original_bundle, binary_format.Kind.bundle
                ).records[1:]
                if original_bundle.startswith(binary_format.MAGIC)
                else []
            )
            bundle = setup_bundle.SetupBundle(
                setups={n: b'' for n in names},
                page_ram2=[0] * count,
                memory=target.memory,
                memory_margin=setup_bundle.HEADER.unpack_from(core)[5],
                metadata=metadata,
            ).pack()
        library = songs.reindex(library, list(upgraded))
        if undo:
            undo = songs.reindex(undo, list(upgraded))
    originals = {
        p.relative_to(directory).as_posix(): sha256(p.read_bytes()).hexdigest()
        for p in sorted(directory.rglob('*'))
        if p.is_file() and p.name not in ('conversion.toml', 'conversion-pending.toml')
    }
    destination.mkdir(parents=True)
    write_record(
        destination / 'conversion-pending.toml', {'source': str(directory.resolve())}
    )
    shutil.copytree(directory, destination, dirs_exist_ok=True)
    (destination / 'songs.bin').write_bytes(library)
    if bundle:
        (destination / 'bundle.bin').write_bytes(bundle)
    if undo:
        (destination / 'songs-undo.bin').write_bytes(undo)
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
    report = Conversion(
        format=binary_format.format_name(binary_format.DEVELOPMENT_FORMAT),
        complete=True,
        files=[
            ConvertedFile(
                path=p.relative_to(destination).as_posix(),
                source_format=artifact_format(directory / p.relative_to(destination))
                if p.relative_to(destination).as_posix() in originals
                else 'generated',
                destination_format=artifact_format(p),
                source_sha256=originals.get(p.relative_to(destination).as_posix(), ''),
                destination_sha256=sha256(p.read_bytes()).hexdigest(),
            )
            for p in sorted(destination.rglob('*'))
            if p.is_file()
            and p.name not in ('conversion.toml', 'conversion-pending.toml')
        ],
    )
    write_record(destination / 'conversion.toml', report.model_dump())
    (destination / 'conversion-pending.toml').unlink()
    check_conversion(destination)
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
