"""Back up pedal data and upgrade supported setup formats without recompiling."""

from __future__ import annotations

import shutil
import struct
import sys
from contextlib import nullcontext
from hashlib import sha256
from json import JSONDecodeError
from pathlib import Path
from typing import Annotated
from uuid import uuid4

import tomlkit
import tyro
from pydantic import BaseModel, Field, ValidationError
from tomlkit.exceptions import ParseError

from ..localization import catalogue
from . import (
    binary_format,
    compiler_inputs,
    compiler_wire,
    data_files,
    device_globals,
    history,
    midi_assets,
    models,
    resolved,
    setup_bundle,
    songs,
    source_archive,
    storage_info,
    transport,
)
from .setup_sync_store import SyncFileError, read_sync_file, sync_files, write_record


class BackupError(ValueError):
    """A backup operation could not proceed with the available data."""


class BackupDirectoryExists(FileExistsError):
    """A new backup could not be started because its directory already exists."""


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
    if _exists(directory / 'backup-pending.toml'):
        raise BackupError(f'{directory}: interrupted backup')
    if _exists(directory / 'conversion-pending.toml'):
        raise BackupError(
            f'{directory}: interrupted conversion; retain the original backup'
        )
    report = directory / 'conversion.toml'
    if not _exists(report):
        report = directory / 'backup.toml'
        present = _exists(report)
        if raw_recovery and not present:
            raise BackupError(f'{directory}: missing raw recovery backup manifest')
        if present:
            backup = _read_model(report, BackupManifest)
            if not backup.complete and not (raw_recovery and backup.raw_state):
                raise BackupError(
                    f'{directory}: incomplete backup; retain raw files for recovery'
                )
            for record in backup.files:
                with sync_files():
                    path = (directory / record.path).resolve()
                    root = directory.resolve()
                if (
                    not path.is_relative_to(root)
                    or not _is_file(path)
                    or sha256(read_sync_file(path)).hexdigest() != record.sha256
                ):
                    raise BackupError(f'{record.path}: backup file checksum differs')
        return
    conversion = _read_model(report, Conversion)
    if not conversion.complete:
        raise BackupError(f'{directory}: incomplete conversion')
    for record in conversion.files:
        with sync_files():
            path = (directory / record.path).resolve()
            root = directory.resolve()
        if not path.is_relative_to(root) or not _is_file(path):
            raise BackupError(
                f'{record.path}: converted file missing or outside backup'
            )
        if sha256(read_sync_file(path)).hexdigest() != record.destination_sha256:
            raise BackupError(f'{record.path}: converted file checksum differs')


def recording_high_water(directory: Path) -> int:
    """Retain recording-name uniqueness without restoring a stale counter sector."""
    minimum = 0
    counter = directory / 'starts.toml'
    if _exists(counter):
        data = read_sync_file(counter)
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
            raise BackupError(f'{counter}: no valid startup counter record')
    for path in _glob(directory / 'setups/midi', '*.mid'):
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
    with sync_files(), path.open('rb') as source:
        header = source.read(binary_format.HEADER.size)
    return binary_format.identify(header)


def backup_pedal(
    directory: Path, port: Path, connection: transport.SerialConnection | None = None
) -> None:
    _create_directory(directory)
    write_record(directory / 'backup-pending.toml', {'complete': False})
    setups = directory / 'setups'
    with sync_files():
        setups.mkdir()
    print(f'Backup: {directory}', flush=True)
    with (
        nullcontext(connection)
        if connection is not None
        else transport.SerialConnection(port, timeout=5) as connection
    ):
        client = transport.SetupClient(connection)
        try:
            status = client.request('CNTRL STATUS')
        except transport.UsbReplyError as error:
            status = str(error)
            if not status.startswith('CNTRL ERROR '):
                raise
        recovery_only = status.startswith('CNTRL ERROR')
        if recovery_only:
            deployment = client.request('CNTRL DEPLOY STATUS').split()
            if len(deployment) != 5 or deployment[:2] != ['CNTRL', 'DEPLOY']:
                raise transport.UsbReplyError(
                    f'{status}; raw recovery protocol is unavailable'
                )
            try:
                root = int(deployment[2])
            except ValueError as error:
                raise transport.UsbReplyError(
                    'invalid persistent root in deployment status'
                ) from error
            status += f' persistent_format=unknown persistent_root={root}'
        _write_file(directory / 'status.txt', status.encode())
        if ' starts=' in status:
            _write_file(directory / 'starts.toml', client.download_starts())
        backup_stored_assets(client, directory)
        languages_valid = True
        archived_languages: list[Path] = []
        if 'language=1' in status.split():
            report = _read_model(directory / 'storage.toml', storage_info.StorageInfo)
            language_root = (persistent_prefix(status) or '/cntrl/') + 'languages'
            for entry in report.entries:
                stored = Path(entry.path)
                if (
                    entry.directory
                    or stored.parent.as_posix() != language_root
                    or stored.suffix != '.bin'
                ):
                    continue
                tag = stored.stem
                try:
                    pack = client.download_language(tag)
                except catalogue.IncompatibleCatalogueError as error:
                    archived_languages.append(
                        directory / 'raw' / entry.path.lstrip('/')
                    )
                    print(f'Archiving language pack {tag}: {error}', file=sys.stderr)
                    continue
                except (*transport.USB_ERRORS, catalogue.CatalogueError) as error:
                    languages_valid = False
                    print(f'Retained raw language pack {tag}: {error}', file=sys.stderr)
                    continue
                path = directory / 'raw/cntrl/languages' / f'{tag}.bin'
                with sync_files():
                    path.parent.mkdir(parents=True, exist_ok=True)
                _write_file(path, pack)
        raw_state = backup_state(client, directory, status)
        languages_valid = languages_valid and all(
            _is_file(p) for p in archived_languages
        )
        if recovery_only:
            write_backup_manifest(directory, complete=False, raw_state=raw_state)
            print(
                'Raw recovery backup retained; active data is unreadable.', flush=True
            )
            return
        if device_globals.supported(status):
            _write_file(
                directory / 'device-globals.toml',
                device_globals.dump(client.global_settings()),
            )
        selected = client.current()
        entries = client.catalogue()
        _write_file(directory / 'selected.txt', selected.encode())
        _write_file(
            directory / 'catalogue.txt',
            '\n'.join(f'{e.name} {e.valid}' for e in entries).encode(),
        )
        with sync_files():
            fonts = {int(p.stem, 16) for p in (setups / 'fonts').glob('*.cfont')}
            movies = {int(p.stem, 16) for p in (setups / 'movies').glob('*.cnsa')}
        healthy = languages_valid and all(e.valid for e in entries)
        snapshots: list[dict[str, object]] = []
        source_snapshots: set[str] = set()
        current_entries = {e.name: e for e in entries}
        history_names = {n for n, _ in history.reserved_numbers(directory)}
        for name in sorted(current_entries.keys() | history_names, key=str.lower):
            entry = current_entries.get(name)
            if entry is not None and entry.valid:
                data = client.download(name)
                if (source := source_archive.identifier(data)) is not None:
                    source_snapshots.add(source)
                _write_file(setups / f'{name}.bin', data)
                try:
                    readable = resolved.upgrade(data, migrate_retired_pages=True)
                    text = resolved.export(readable)
                    _write_file(setups / f'{name}.toml', text.encode())
                    backup_assets(client, readable, setups, fonts, movies)
                except (
                    binary_format.DocumentFormatError,
                    transport.UsbReplyError,
                    transport.UsbTransferInputError,
                ) as error:
                    healthy = False
                    print(f'{name}: retained raw setup: {error}', file=sys.stderr)
                print(f'Saved {name}: {len(data)} bytes', flush=True)
            elif entry is not None:
                print(f'Skipped invalid setup: {name}', flush=True)
            for revision in history.revisions(client, name):
                data = client.download(name, revision.number)
                if (source := source_archive.identifier(data)) is not None:
                    source_snapshots.add(source)
                if len(data) != revision.length:
                    raise transport.UsbReplyError(
                        f'{name}/{revision.number}: history length differs'
                    )
                folder = directory / 'history' / name
                with sync_files():
                    folder.mkdir(parents=True, exist_ok=True)
                _write_file(folder / f'{revision.number}.bin', data)
                try:
                    version = struct.unpack_from(
                        '<H',
                        binary_format.core(data),
                        4,
                    )[0]
                except binary_format.DocumentFormatError:
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
                _write_file(
                    directory / 'history.toml',
                    tomlkit.dumps({'revisions': snapshots}).encode(),
                )
                if version in binary_format.SETUP_VERSIONS:
                    readable = resolved.upgrade(data, migrate_retired_pages=True)
                    _write_file(
                        folder / f'{revision.number}.toml',
                        resolved.export(readable).encode(),
                    )
                    backup_assets(client, readable, setups, fonts, movies)
                print(
                    f'Saved {name} revision {revision.number}: {len(data)} bytes',
                    flush=True,
                )
        _write_file(
            directory / 'history.toml', tomlkit.dumps({'revisions': snapshots}).encode()
        )
        library = songs.download(client)
        undo = songs.download(client, undo=True) if raw_state else b''
        _write_file(directory / 'songs.bin', library)
        if undo:
            _write_file(directory / 'songs-undo.bin', undo)
        for saved_library in (library, undo):
            if not saved_library:
                continue
            try:
                source_snapshots.update(songs.source_snapshots(saved_library))
            except (binary_format.DocumentFormatError, songs.SongLibraryError) as error:
                healthy = False
                print(f'Retained raw song library: {error}', file=sys.stderr)
        for source in sorted(source_snapshots):
            path = directory / 'raw/cntrl/source' / f'{source}.bin'
            with sync_files():
                present = path.exists()
                if not present:
                    path.parent.mkdir(parents=True, exist_ok=True)
            if not present:
                _write_file(path, client.download_source(source))
            content = read_sync_file(path)
            if sha256(content).hexdigest() != source:
                raise BackupError(f'{source}: source archive checksum mismatch')
            source_archive.read(content)
        if raw_state and (bundle := client.download_bundle()):
            _write_file(directory / 'bundle.bin', bundle)
        write_backup_manifest(directory, complete=healthy, raw_state=raw_state)
    result = 'complete' if healthy else 'incomplete; raw recovery data retained'
    print(f'Selected: {selected}\n{status}\nBackup {result}.', flush=True)


def write_backup_manifest(directory: Path, *, complete: bool, raw_state: bool) -> None:
    with sync_files():
        files = [p for p in sorted(directory.rglob('*')) if p.is_file()]
    manifest = BackupManifest(
        complete=complete,
        raw_state=raw_state,
        files=[
            SavedFile(
                path=p.relative_to(directory).as_posix(),
                sha256=sha256(read_sync_file(p)).hexdigest(),
            )
            for p in files
            if p.name not in ('backup-pending.toml', 'backup.toml')
        ],
    )
    write_record(directory / 'backup.toml', manifest.model_dump())
    with sync_files():
        (directory / 'backup-pending.toml').unlink()


def persistent_prefix(status: str) -> str:
    try:
        root = next(
            (
                int(v.split('=', 1)[1])
                for v in status.split()
                if v.startswith('persistent_root=')
            ),
            0,
        )
    except ValueError as error:
        raise transport.UsbReplyError(
            'invalid persistent root in pedal status'
        ) from error
    return f'/cntrl/generations/{root:08x}/' if root else ''


def backup_state(client: transport.SetupClient, directory: Path, status: str) -> bool:
    """Preserve active journals and adapter files without interpreting their bytes."""
    if ' persistent_format=' not in status:
        return False
    report = _read_model(directory / 'storage.toml', storage_info.StorageInfo)
    prefix = persistent_prefix(status)
    for entry in report.entries:
        if entry.directory or not entry.path.startswith('/cntrl/'):
            continue
        if '/generations/' in entry.path and not entry.path.startswith(prefix):
            continue
        logical = (
            '/cntrl/' + entry.path[len(prefix) :]
            if prefix and entry.path.startswith(prefix)
            else entry.path
        )
        if (
            prefix
            and logical == entry.path
            and logical.startswith(
                (
                    '/cntrl/setups/',
                    '/cntrl/history/',
                    '/cntrl/songs/',
                    '/cntrl/selection/',
                    '/cntrl/global/',
                    '/cntrl/languages/',
                )
            )
        ):
            continue
        asset = Path(logical)
        with sync_files():
            if (
                asset.parent.as_posix() == '/cntrl/languages'
                and (directory / 'raw/cntrl/languages' / asset.name).exists()
            ):
                continue
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
        _write_file(path, client.download_state(entry.path))
    return True


def backup_assets(
    client: transport.SetupClient,
    data: bytes,
    folder: Path,
    fonts: set[int],
    movies: set[int],
) -> None:
    from .cli import save_asset

    source = tomlkit.parse(resolved.export(data)).unwrap()
    try:
        settings = models.Setup.model_validate(source)
    except ValidationError as error:
        raise binary_format.DocumentFormatError(
            f'invalid compiled setup asset references: {error}'
        ) from error
    for value in settings.fonts.values():
        try:
            key = int(Path(value).stem, 16)
        except ValueError as error:
            raise binary_format.DocumentFormatError(
                f'invalid font reference: {value}'
            ) from error
        if key not in fonts:
            save_asset(folder / 'fonts' / f'{key:08x}.cfont', client.download_font(key))
            fonts.add(key)
    for value in settings.splash_movie + settings.idle_movie:
        try:
            key = int(Path(value).stem, 16)
        except ValueError as error:
            raise binary_format.DocumentFormatError(
                f'invalid movie reference: {value}'
            ) from error
        if key not in movies:
            save_asset(folder / value, client.download_movie(key))
            movies.add(key)
    for name in midi_assets.filenames(source):
        destination = folder / 'midi' / name
        with sync_files():
            present = destination.exists()
        if not present:
            save_asset(destination, client.download_midi(name))


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
    _write_file(
        directory / 'storage.toml',
        tomlkit.dumps(report.model_dump(exclude_none=True)).encode(),
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
            except (*transport.USB_ERRORS, transport.UsbTransferInputError) as error:
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
            except transport.USB_ERRORS as error:
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
            except transport.USB_ERRORS as error:
                unavailable.append(entry.path)
                print(f'Not backed up: {entry.path}: {error}', file=sys.stderr)
                continue
            save_asset(
                directory / 'setups/movies' / f'{key:08x}.cnsa',
                data,
            )
    if unavailable:
        _write_file(
            directory / 'unavailable-assets.txt',
            ('\n'.join(unavailable) + '\n').encode(),
        )


def _write_file(path: Path, data: bytes) -> None:
    with sync_files():
        path.write_bytes(data)


def _create_directory(path: Path) -> None:
    try:
        path.mkdir(parents=True)
    except FileExistsError as error:
        raise BackupDirectoryExists(
            error.errno, error.strerror, error.filename
        ) from error
    except OSError as error:
        raise SyncFileError(str(error)) from error


def _exists(path: Path) -> bool:
    with sync_files():
        return path.exists()


def _is_file(path: Path) -> bool:
    with sync_files():
        return path.is_file()


def _glob(path: Path, pattern: str) -> list[Path]:
    with sync_files():
        return sorted(path.glob(pattern))


def _read_text(path: Path) -> str:
    data = read_sync_file(path)
    try:
        return data.decode()
    except UnicodeDecodeError as error:
        raise BackupError(f'{path}: {error}') from error


def _read_model[Document: BaseModel](path: Path, model: type[Document]) -> Document:
    try:
        return model.model_validate(tomlkit.parse(_read_text(path)).unwrap())
    except (ParseError, ValidationError) as error:
        raise BackupError(f'{path}: {error}') from error


def _asset_key(path: Path) -> int:
    try:
        return int(path.stem, 16)
    except ValueError as error:
        raise BackupError(f'{path}: invalid asset checksum in filename') from error


def _read_fonts(directory: Path) -> dict[int, bytes]:
    return {
        _asset_key(p): read_sync_file(p)
        for p in _glob(directory / 'setups/fonts', '*.cfont')
    }


def read_target(path: Path) -> models.Target:
    try:
        document = compiler_inputs.read_document(path)
    except (OSError, UnicodeDecodeError, JSONDecodeError, ParseError) as error:
        raise BackupError(f'{path}: {error}') from error
    try:
        return models.Target.model_validate(document)
    except ValidationError as error:
        raise BackupError(f'{path}: {error}') from error


def restore_setups(
    directory: Path,
    port: Path,
    target: models.Target,
    *,
    preserve_memory_estimates: bool = False,
    connection: transport.SerialConnection | None = None,
) -> None:
    check_conversion(directory)
    minimum_starts = recording_high_water(directory)
    overlay = directory / 'device-globals.toml'
    restored_globals = (
        device_globals.load_saved(overlay, read_sync_file(overlay))
        if _exists(overlay)
        else None
    )
    folder = directory / 'setups'
    selected = _read_text(directory / 'selected.txt')
    fonts = _read_fonts(directory)
    setups = {
        n: resolved.prepare_restore(
            d,
            fonts,
            target=target,
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
                target=target,
                preserve_memory_estimates=preserve_memory_estimates,
            ),
        )
        for n, r, d in read_history_binaries(directory)
    ]
    library = songs.upgrade(
        read_sync_file(directory / 'songs.bin'),
        fonts,
        target=target,
        preserve_memory_estimates=preserve_memory_estimates,
    )
    undo_path = directory / 'songs-undo.bin'
    undo = (
        songs.upgrade(
            read_sync_file(undo_path),
            fonts,
            target=target,
            preserve_memory_estimates=preserve_memory_estimates,
        )
        if _exists(undo_path)
        else b''
    )
    validate_assets(
        directory, [*setups.values(), *(d for _, _, d in snapshots)], [library, undo]
    )
    if selected and selected not in setups:
        raise BackupError(f'{selected}: saved selection is absent from the backup')
    with (
        nullcontext(connection)
        if connection is not None
        else transport.SerialConnection(port) as connection
    ):
        client = transport.SetupClient(connection)
        active_ready = client.recover_deployment()
        recovery = (
            directory.parent / f'{directory.name}-before-restore-{uuid4().hex[:12]}'
        )
        backup_pedal(recovery, port, connection)
        current_overlay = recovery / 'device-globals.toml'
        if restored_globals is None and _exists(current_overlay):
            restored_globals = device_globals.load_saved(
                current_overlay, read_sync_file(current_overlay)
            )
        language_packs = {
            p.stem: read_sync_file(p)
            for p in _glob(recovery / 'raw/cntrl/languages', '*.bin')
        }
        language_packs.update(
            {
                p.stem: read_sync_file(p)
                for p in _glob(directory / 'raw/cntrl/languages', '*.bin')
            }
        )
        if language_packs:
            from ..localization.catalogue import inspect

            for tag, pack in language_packs.items():
                if inspect(pack).language != tag:
                    raise BackupError(f'{tag}: language pack tag differs from filename')
        snapshot = _read_model(recovery / 'backup.toml', BackupManifest)
        active_ready = active_ready and snapshot.complete
        if not active_ready and not _exists(directory / 'conversion.toml'):
            raise BackupError(
                'recovery installation requires an offline converted backup'
            )
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
                    raise BackupError(
                        f'{name}/{revision.number}: history conflicts with device; '
                        f'backup: {recovery}'
                    )
            previous[key] = (name, revision, data)
        snapshots = list(previous.values())
        current_bundle = client.download_bundle() if active_ready else b''
        existing = setup_bundle.unpack(current_bundle) if current_bundle else {}
        combined = setup_bundle.reindex(existing | setups)
        template_path = directory / 'bundle.bin'
        has_template = _exists(template_path)
        template = read_sync_file(template_path) if has_template else current_bundle
        if current_bundle and has_template and existing.keys() - setups.keys():
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
                raise BackupError(
                    'cannot merge different opaque bundle metadata; '
                    f'original retained in {recovery}'
                )
        library = songs.reindex(library, list(combined))
        if undo:
            undo = songs.reindex(undo, list(combined))
        bundle_data = b''
        if not combined and has_template:
            bundle_data = template
            if setup_bundle.unpack(bundle_data):
                raise BackupError(
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
        if _exists(report_path):
            report = _read_model(report_path, storage_info.StorageInfo)
            lengths = [len(bundle_data) + 100] * 2
            lengths.extend([len(library) + 28, len(library) + 28, len(undo) + 28])
            lengths.extend(len(history.pack_revision(n, r, d)) for n, r, d in snapshots)
            stored = {e.path for e in report.entries if not e.directory}
            for path in _glob(directory / 'raw/cntrl/source', '*.bin'):
                if f'/cntrl/source/{path.name}' not in stored:
                    with sync_files():
                        lengths.append(path.stat().st_size)
            lengths.extend(len(p) for p in language_packs.values())
            for kind, pattern in (
                ('fonts', '*.cfont'),
                ('movies', '*.cnsa'),
                ('midi', '*'),
            ):
                for path in _glob(folder / kind, pattern):
                    name = f'{path.stem}.bin' if kind == 'movies' else path.name
                    if f'/cntrl/{kind}/{name}' not in stored:
                        with sync_files():
                            lengths.append(path.stat().st_size)
            # Includes both activation slots, selection journals, directory
            # clusters, and a replacement/upload slot while journals advance.
            directories = 16 + len(combined) * 2 + len(reservations) + len(snapshots)
            directories += bool(language_packs)
            if restored_globals is not None:
                lengths.extend([len(device_globals.journal(restored_globals))] * 2)
                directories += 1
            check_deployment_space(report, lengths + [96, 96, 24, 24], directories)
        for key, data in fonts.items():
            client.copy_font(key, data)
        for p in _glob(folder / 'movies', '*.cnsa'):
            client.copy_movie(read_sync_file(p))
        for p in _glob(folder / 'midi', '*'):
            client.copy_midi(p.name, read_sync_file(p), overwrite=False)
        for p in _glob(directory / 'raw/cntrl/source', '*.bin'):
            content = read_sync_file(p)
            source_archive.read(content)
            client.copy_source(p.stem, content)
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
        for tag, pack in language_packs.items():
            client.copy_language(tag, pack)
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
                raise BackupError(
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
                raise BackupError('restored selection does not match the backup')
            print(f'Selected: {selected}', flush=True)
        if restored_globals is not None:
            client.replace_globals(restored_globals)
        client.commit_deployment(bundle_data, library)
        print(f'Deployment committed. Recovery backup: {recovery}', flush=True)


def verify_after_flashing(
    directory: Path,
    port: Path,
    target: models.Target,
    *,
    preserve_memory_estimates: bool = False,
    connection: transport.SerialConnection | None = None,
) -> None:
    check_conversion(directory)
    selected = _read_text(directory / 'selected.txt')
    expected_catalogue = _read_text(directory / 'catalogue.txt')
    fonts = _read_fonts(directory)
    setups = {
        n: resolved.prepare_restore(
            d,
            fonts,
            target=target,
            preserve_memory_estimates=preserve_memory_estimates,
        )
        for n, d in read_setup_binaries(directory).items()
    }
    library = songs.upgrade(
        read_sync_file(directory / 'songs.bin'),
        fonts,
        target=target,
        preserve_memory_estimates=preserve_memory_estimates,
    )
    with (
        nullcontext(connection)
        if connection is not None
        else transport.SerialConnection(port) as connection
    ):
        client = transport.SetupClient(connection)
        status = client.request('CNTRL STATUS')
        overlay = directory / 'device-globals.toml'
        if _exists(overlay) and client.global_settings() != device_globals.load_saved(
            overlay, read_sync_file(overlay)
        ):
            raise BackupError('device-global settings differ from the backup')
        parts = status.split()
        if len(parts) < 5 or parts[:3] != [
            'CNTRL',
            'READY',
            str(compiler_wire.DOCUMENT_VERSION),
        ]:
            raise BackupError(
                f'expected READY format {compiler_wire.DOCUMENT_VERSION}; '
                f'received {status}'
            )
        actual = client.current()
        if actual != selected:
            raise BackupError(
                f'selection differs: expected {selected!r}, received {actual!r}'
            )
        actual_catalogue = '\n'.join(f'{e.name} {e.valid}' for e in client.catalogue())
        if actual_catalogue != expected_catalogue:
            raise BackupError(
                f'setup catalogue differs:\nExpected:\n{expected_catalogue}\n'
                f'Received:\n{actual_catalogue}'
            )
        for name, data in setups.items():
            if binary_format.wrap(client.download(name)) != binary_format.wrap(data):
                raise BackupError(f'{name}: setup binary differs from the backup')
            print(f'{name}: verified identical', flush=True)
        for key, data in fonts.items():
            if client.download_font(key) != data:
                raise BackupError(f'{key:08x}: font differs from the backup')
        for p in _glob(directory / 'setups/movies', '*.cnsa'):
            if client.download_movie(_asset_key(p)) != read_sync_file(p):
                raise BackupError(f'{p.name}: movie differs from the backup')
        for p in _glob(directory / 'setups/midi', '*'):
            if client.download_midi(p.name) != read_sync_file(p):
                raise BackupError(f'{p.name}: MIDI file differs from the backup')
        for p in _glob(directory / 'raw/cntrl/source', '*.bin'):
            if client.download_source(p.stem) != read_sync_file(p):
                raise BackupError(f'{p.name}: source archive differs from the backup')
        for p in _glob(directory / 'raw/cntrl/languages', '*.bin'):
            if client.download_language(p.stem) != read_sync_file(p):
                raise BackupError(f'{p.name}: language pack differs from the backup')
        listed: dict[str, list[history.Revision]] = {}
        for name, revision, data in read_history_binaries(directory):
            _, expected = history.normalize_revision(
                name,
                resolved.prepare_restore(
                    data,
                    fonts,
                    target=target,
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
                raise BackupError(
                    f'{name}/{revision.number}: history differs from the backup'
                )
            if actual_revision != revision.model_copy(
                update={'length': len(actual_data)}
            ):
                raise BackupError(
                    f'{name}/{revision.number}: '
                    'history metadata differs from the backup'
                )
        if songs.download(client) != library:
            raise BackupError('song library differs from the backup')
    print(
        f'{status}\nSelection, setups, history, assets and songs verified unchanged.',
        flush=True,
    )


def read_setup_binaries(directory: Path) -> dict[str, bytes]:
    folder = directory / 'setups'
    with sync_files():
        if not folder.is_dir():
            raise BackupError(f'{folder}: missing backup setup directory')
    setups = {s.stem: read_sync_file(s) for s in _glob(folder, '*.bin')}
    for name, data in setups.items():
        try:
            resolved.graph_offset(resolved.upgrade(data, migrate_retired_pages=True))
        except binary_format.DocumentFormatError as error:
            raise BackupError(f'{folder / name}: {error}') from error
    return setups


def validate_assets(
    directory: Path, setups: list[bytes], libraries: list[bytes]
) -> None:
    """Require every executable document's assets before deployment or conversion."""
    needed: set[str] = set()
    overlay_path = directory / 'device-globals.toml'
    language = (
        device_globals.load_saved(overlay_path, read_sync_file(overlay_path)).language
        if _exists(overlay_path)
        else None
    )
    if language is not None and language != 'en':
        from ..localization.catalogue import inspect

        pack = directory / 'raw/cntrl/languages' / f'{language}.bin'
        if not _is_file(pack) or inspect(read_sync_file(pack)).language != language:
            raise BackupError(
                f'{language}: language pack is absent or invalid in the backup'
            )
    sources: set[str] = set()
    for library in libraries:
        if library:
            sources.update(songs.source_snapshots(library))
            needed.update(
                p for v in songs.asset_references(library).values() for p in v
            )
    for data in setups:
        if (source := source_archive.identifier(data)) is not None:
            sources.add(source)
        source_data = tomlkit.parse(resolved.export(data)).unwrap()
        try:
            settings = models.Setup.model_validate(source_data)
        except ValidationError as error:
            raise binary_format.DocumentFormatError(
                f'invalid compiled setup asset references: {error}'
            ) from error
        needed.update(settings.fonts.values())
        needed.update(settings.splash_movie + settings.idle_movie)
        needed.update(f'midi/{n}' for n in midi_assets.filenames(source_data))
    for source in sources:
        archive = directory / 'raw/cntrl/source' / f'{source}.bin'
        if not _is_file(archive):
            raise BackupError(
                f'{source}: required source archive is absent from the backup'
            )
        content = read_sync_file(archive)
        if sha256(content).hexdigest() != source:
            raise BackupError(f'{source}: source archive checksum mismatch')
        source_archive.read(content)
    with sync_files():
        asset_root = (directory / 'setups').resolve()
    for name in sorted(needed):
        with sync_files():
            path = (asset_root / name).resolve()
        if not path.is_relative_to(asset_root) or not _is_file(path):
            raise BackupError(f'{name}: required asset is absent from the backup')


def check_deployment_space(
    report: storage_info.StorageInfo, lengths: list[int], directories: int
) -> None:
    """Budget staging without counting retained committed files as reclaimable."""
    if report.free_bytes is None:
        raise BackupError('deployment requires a storage scan with free space')
    unit = report.allocation_unit_bytes
    required = (directories + sum((n + unit - 1) // unit for n in lengths)) * unit
    if required > report.free_bytes:
        raise BackupError(
            f'deployment needs {required} free bytes including staging; '
            f'only {report.free_bytes} are available'
        )


def prepare_restore(directory: Path, destination: Path, target: models.Target) -> Path:
    """Validate a backup and prepare current-format copies, retaining originals."""
    check_conversion(directory)
    recording_high_water(directory)
    with sync_files():
        source_root = directory.resolve()
        if destination.resolve().is_relative_to(source_root):
            raise BackupError(
                'conversion destination must be outside the original backup'
            )
    setups = read_setup_binaries(directory)
    snapshots = read_history_binaries(directory)
    fonts = _read_fonts(directory)
    library = songs.upgrade(
        read_sync_file(directory / 'songs.bin'),
        fonts,
        target=target,
    )
    undo_path = directory / 'songs-undo.bin'
    undo = (
        songs.upgrade(read_sync_file(undo_path), fonts, target=target)
        if _exists(undo_path)
        else b''
    )
    selected = _read_text(directory / 'selected.txt')
    if selected and selected not in setups:
        raise BackupError(f'{selected}: saved selection is absent from the backup')
    upgraded = {
        n: binary_format.wrap(resolved.prepare_restore(d, fonts, target=target))
        for n, d in setups.items()
    }
    revisions = [
        (
            n,
            r,
            binary_format.wrap(resolved.prepare_restore(d, fonts, target=target)),
        )
        for n, r, d in snapshots
    ]
    validate_assets(
        directory, [*upgraded.values(), *(d for _, _, d in revisions)], [library, undo]
    )
    bundle = b''
    bundle_path = directory / 'bundle.bin'
    if _exists(bundle_path):
        original_bundle = read_sync_file(bundle_path)
        if setup_bundle.unpack(original_bundle) != setups:
            raise BackupError(f'{bundle_path}: bundle and authoritative setups differ')
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
    with sync_files():
        original_paths = [
            p
            for p in sorted(directory.rglob('*'))
            if p.is_file()
            and p.name not in ('conversion.toml', 'conversion-pending.toml')
        ]
    originals = {
        p.relative_to(directory).as_posix(): sha256(read_sync_file(p)).hexdigest()
        for p in original_paths
    }
    _create_directory(destination)
    write_record(destination / 'conversion-pending.toml', {'source': str(source_root)})
    with sync_files():
        shutil.copytree(directory, destination, dirs_exist_ok=True)
    _write_file(destination / 'songs.bin', library)
    if bundle:
        _write_file(destination / 'bundle.bin', bundle)
    if undo:
        _write_file(destination / 'songs-undo.bin', undo)
    for name, data in upgraded.items():
        _write_file(destination / 'setups' / f'{name}.bin', data)
        _write_file(
            destination / 'setups' / f'{name}.toml', resolved.export(data).encode()
        )
    manifest: list[dict[str, object]] = []
    for name, revision, data in revisions:
        folder = destination / 'history' / name
        _write_file(folder / f'{revision.number}.bin', data)
        _write_file(folder / f'{revision.number}.toml', resolved.export(data).encode())
        manifest.append(
            revision.model_dump()
            | {
                'name': name,
                'number': str(revision.number),
                'length': len(data),
                'version': compiler_wire.DOCUMENT_VERSION,
            }
        )
    _write_file(
        destination / 'history.toml', tomlkit.dumps({'revisions': manifest}).encode()
    )
    with sync_files():
        converted_paths = [
            p
            for p in sorted(destination.rglob('*'))
            if p.is_file()
            and p.name not in ('conversion.toml', 'conversion-pending.toml')
        ]
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
                destination_sha256=sha256(read_sync_file(p)).hexdigest(),
            )
            for p in converted_paths
        ],
    )
    write_record(destination / 'conversion.toml', report.model_dump())
    with sync_files():
        (destination / 'conversion-pending.toml').unlink()
    check_conversion(destination)
    return destination


def read_history_binaries(directory: Path) -> list[tuple[str, history.Revision, bytes]]:
    path = directory / 'history.toml'
    if not _exists(path):
        return []
    try:
        document = tomlkit.parse(_read_text(path)).unwrap()
    except ParseError as error:
        raise BackupError(f'{path}: {error}') from error
    snapshots = document.get('revisions', [])
    if not isinstance(snapshots, list):
        raise BackupError('invalid history backup manifest')
    result = []
    for entry in snapshots:
        if not isinstance(entry, dict) or 'name' not in entry:
            raise BackupError('invalid history backup entry')
        name = data_files.validate_name(str(entry['name']))
        try:
            revision = history.Revision.model_validate(entry)
        except ValidationError as error:
            raise BackupError(f'{path}: {error}') from error
        data = read_sync_file(directory / 'history' / name / f'{revision.number}.bin')
        if len(data) != revision.length:
            raise BackupError(
                f'{name}/{revision.number}: history backup length differs'
            )
        resolved.graph_offset(resolved.upgrade(data, migrate_retired_pages=True))
        result.append((name, revision, data))
    return result
