"""Persist setup sync reviews, baselines, and interrupted publications."""

import os
from collections.abc import Generator, Mapping
from contextlib import contextmanager
from enum import StrEnum, auto
from hashlib import sha256
from pathlib import Path
from tempfile import NamedTemporaryFile, mkdtemp
from typing import Literal, cast

import tomlkit
from pydantic import BaseModel, Field, StrictBool, ValidationError
from tomlkit.exceptions import ParseError
from tomlkit.toml_document import TOMLDocument

from ..file_lock import writer_lock
from . import binary_format, data_files, device_globals, global_overlay, resolved


class SyncError(ValueError):
    """A saved review or sync operation requires user attention."""


class SyncFileError(OSError):
    """A sync file could not be read, written, or removed."""


class RecoverySource(BaseModel, frozen=True):
    file: Path
    before: str
    after: str
    present: StrictBool = True


class RecoveryJournal(BaseModel, frozen=True):
    review: Path
    sources: list[RecoverySource]


class Choice(StrEnum):
    host = auto()
    pedal = auto()
    unresolved = auto()


class Value(BaseModel, frozen=True):
    """Represent absence separately from any TOML value."""

    present: bool = False
    value: object = ''


class Change(BaseModel, frozen=True):
    setup: str
    path: list[str | int]
    base: Value
    host: Value
    pedal: Value
    choice: Choice
    destination: data_files.Location | None = None
    affects: list[str] = Field(default_factory=list)
    note: str = ''
    remove_inherited: bool = False
    scope: Literal['setup', 'device-global'] = 'setup'
    setting_override: Literal['none', 'set', 'inherit'] = 'none'
    setting_field: int | None = None


class Source(BaseModel, frozen=True):
    file: Path
    before: str
    present: bool = True


class Review(BaseModel, frozen=True):
    root: Path
    recipe: data_files.Sources
    baseline: str
    sources: list[Source]
    host: dict[str, str]
    pedal: dict[str, str]
    changes: list[Change]
    global_snapshot: str | None = None


@contextmanager
def sync_files() -> Generator[None]:
    try:
        yield
    except SyncFileError:
        raise
    except OSError as error:
        raise SyncFileError(str(error)) from error


def read_sync_file(path: Path) -> bytes:
    with sync_files():
        return path.read_bytes()


def parse_record(path: Path, data: bytes) -> TOMLDocument:
    try:
        return tomlkit.parse(data.decode())
    except (ParseError, UnicodeDecodeError) as error:
        raise SyncError(f'{path}: {error}') from error


def read_review(path: Path) -> Review:
    document = parse_record(path, read_sync_file(path))
    try:
        return Review.model_validate(document.unwrap())
    except ValidationError as error:
        raise SyncError(f'{path}: {error}') from error


def digest(data: bytes) -> str:
    return sha256(data).hexdigest()


def write_record(path: Path, value: Mapping[str, object]) -> None:
    data = tomlkit.dumps(value).encode()
    with sync_files():
        replace(path, data)


def replace(path: Path, data: bytes) -> None:
    """Publish one file atomically using a temporary sibling on every platform."""
    with NamedTemporaryFile(
        dir=path.parent, prefix=f'.{path.name}-', delete=False
    ) as output:
        temporary = Path(output.name)
        try:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        except OSError:
            temporary.unlink(missing_ok=True)
            raise
    try:
        if path.exists():
            temporary.chmod(path.stat().st_mode)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def check_pending(root: Path) -> None:
    with sync_files():
        pending = (root / 'pending.toml').exists()
    if pending:
        raise SyncError(
            f'interrupted apply: cntrl setup sync --recover {root / "pending.toml"}'
        )


def base_record(root: Path) -> bytes:
    path = root / 'base.toml'
    with sync_files():
        return path.read_bytes() if path.exists() else b''


def baseline(root: Path) -> dict[str, bytes]:
    record = base_record(root)
    if not record:
        return {}
    files = parse_record(root / 'base.toml', record)['setups']
    result: dict[str, bytes] = {}
    for n, entry in files.items():
        data = read_sync_file(root / entry['file'])
        if digest(data) != entry['checksum']:
            raise SyncError(f'{n}: damaged synchronization baseline')
        result[n] = data
    return result


def global_baseline(root: Path) -> global_overlay.Overlay | None:
    record = base_record(root)
    if record:
        entry = parse_record(root / 'base.toml', record).unwrap().get('device_globals')
        if isinstance(entry, dict):
            path = root / entry['file']
            data = read_sync_file(path)
            if digest(data) != entry['checksum']:
                raise SyncError('damaged device-global synchronization baseline')
            return device_globals.load_saved(path, data)
    recovered = root.parent / 'pedal/device-globals.toml'
    with sync_files():
        present = recovered.exists()
    return (
        device_globals.load_saved(recovered, read_sync_file(recovered))
        if present
        else None
    )


def migrate_baselines(root: Path) -> None:
    """Publish converted baselines together while retaining their source snapshots."""
    check_pending(root)
    original = base_record(root)
    if not original:
        return
    saved = baseline(root)
    converted = {n: binary_format.wrap(resolved.upgrade(d)) for n, d in saved.items()}
    if converted == saved:
        return
    record = parse_record(root / 'base.toml', original).unwrap()
    entries = cast(dict[str, dict[str, str]], record['setups'])
    version = binary_format.format_name(binary_format.DEVELOPMENT_FORMAT)
    with sync_files():
        folder = Path(mkdtemp(prefix=f'format-{version}-', dir=root))
        replace(folder / 'base.before', original)
    write_record(
        folder / 'conversion.toml', {'format': version, 'setups': list(converted)}
    )
    write_record(
        root / 'pending.toml',
        {'review': str(folder / 'conversion.toml'), 'sources': []},
    )
    for name, data in converted.items():
        source = root / entries[name]['file']
        destination = source.parent / f'.converted-{version}-{digest(data)}.bin'
        with sync_files():
            if destination.exists() and destination.read_bytes() != data:
                raise SyncError(f'{destination}: converted baseline checksum differs')
            replace(destination, data)
        entries[name] = {
            'file': str(destination.relative_to(root)),
            'checksum': digest(data),
        }
    write_record(root / 'base.toml', record)
    write_record(folder / 'applied.toml', {'complete': True})
    with sync_files():
        (root / 'pending.toml').unlink()


def advance(root: Path, folder: Path, names: list[str]) -> None:
    record = base_record(root)
    previous = (
        parse_record(root / 'base.toml', record).unwrap() if record else {'setups': {}}
    )
    setups = cast(dict[str, object], previous['setups'])
    for n in names:
        path = folder / f'{n}.bin'
        setups[n] = {
            'file': str(path.relative_to(root)),
            'checksum': digest(read_sync_file(path)),
        }
    globals_path = folder / 'device-globals.toml'
    with sync_files():
        has_globals = globals_path.exists()
    if has_globals:
        previous['device_globals'] = {
            'file': str(globals_path.relative_to(root)),
            'checksum': digest(read_sync_file(globals_path)),
        }
    write_record(root / 'base.toml', previous)


def publish(path: Path, review: Review, candidates: dict[Path, bytes]) -> int:
    sources = []
    for i, (p, after) in enumerate(candidates.items()):
        with sync_files():
            present = p.exists()
            before = p.read_bytes() if present else b''
        if present and before == after:
            continue
        with sync_files():
            (path.parent / f'apply-{i}.before').write_bytes(before)
            (path.parent / f'apply-{i}.after').write_bytes(after)
        sources.append(
            {
                'file': str(p),
                'before': f'apply-{i}.before',
                'after': f'apply-{i}.after',
                'present': present,
            }
        )
    journal = {'review': str(path), 'sources': sources}
    # Check again immediately before the first replacement, after compilation.
    for s in review.sources:
        with sync_files():
            present = s.file.exists()
        if (
            present != s.present
            or digest(read_sync_file(s.file) if s.present else b'') != s.before
        ):
            raise SyncError(f'{s.file}: source changed during apply')
    if digest(base_record(review.root)) != review.baseline:
        raise SyncError('baseline changed during apply')
    before_base = base_record(review.root)
    with sync_files():
        (path.parent / 'base.before').write_bytes(before_base)
    write_record(review.root / 'pending.toml', journal)
    for s in sources:
        with sync_files():
            replace(
                Path(cast(str, s['file'])),
                (path.parent / cast(str, s['after'])).read_bytes(),
            )
    advance(review.root, path.parent, list(review.host))
    write_record(path.parent / 'applied.toml', {'complete': True})
    with sync_files():
        (review.root / 'pending.toml').unlink()
    return len(sources)


def recover(path: Path) -> None:
    with sync_files():
        path = path.resolve()
    with writer_lock(path.parent, operation='setup sync operation'):
        recover_locked(path)


def recover_locked(path: Path) -> None:
    record = parse_record(path, read_sync_file(path)).unwrap()
    try:
        journal = RecoveryJournal.model_validate(record)
    except ValidationError as error:
        raise SyncError(f'{path}: {error}') from error
    folder = journal.review.parent
    # Refuse to discard edits made after an interrupted publication.
    for s in journal.sources:
        with sync_files():
            current = s.file.read_bytes() if s.file.exists() else None
            before = (folder / s.before).read_bytes() if s.present else None
            after = (folder / s.after).read_bytes()
        if current not in (before, after):
            raise SyncError(f'{s.file}: changed after interruption; restore manually')
    with sync_files():
        for s in journal.sources:
            if s.present:
                replace(s.file, (folder / s.before).read_bytes())
            else:
                s.file.unlink(missing_ok=True)
        before = (folder / 'base.before').read_bytes()
        if before:
            replace(path.parent / 'base.toml', before)
        else:
            (path.parent / 'base.toml').unlink(missing_ok=True)
        (folder / 'applied.toml').unlink(missing_ok=True)
        path.unlink()
    print('Restored original sources and baseline. Preview again before applying.')
