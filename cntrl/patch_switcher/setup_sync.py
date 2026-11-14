"""Reviewed three-way imports into the original setup authoring documents."""

import os
from collections.abc import Mapping, MutableMapping, MutableSequence
from copy import deepcopy
from difflib import unified_diff
from enum import StrEnum, auto
from hashlib import sha256
from pathlib import Path
from tempfile import NamedTemporaryFile, mkdtemp
from typing import Annotated, cast

import tomlkit
import tyro
from pydantic import BaseModel, Field

from . import (
    backups,
    binary_format,
    compiler,
    data_files,
    resolved,
    setup_bundle,
    transport,
)


class Choice(StrEnum):
    host = auto()
    pedal = auto()
    unresolved = auto()


class Sync(data_files.Sources, frozen=True):
    """Preview saved pedal edits, then apply a reviewed TOML report to source files.

    Edit each unresolved choice in review.toml to host or pedal, then use --apply.
    --recover restores the originals from an interrupted apply.
    """

    document: Annotated[list[str], tyro.conf.Positional] = Field(default_factory=list)
    target: list[str] = Field(default_factory=list)
    port: Path | None = None
    apply: Path | None = None
    recover: Path | None = None


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


class Source(BaseModel, frozen=True):
    file: Path
    before: str


class Review(BaseModel, frozen=True):
    root: Path
    recipe: data_files.Sources
    baseline: str
    sources: list[Source]
    host: dict[str, str]
    pedal: dict[str, str]
    changes: list[Change]


def canonical(data: bytes) -> dict[str, object]:
    """Decode authoritative MIDI values, excluding stale catalogue annotations."""
    result = tomlkit.parse(resolved.export(data)).unwrap()
    # Program catalogue annotations describe the authored selection, not edits
    # saved into the compiled graph. Profiles still carry actual bank mappings.
    if isinstance(definitions := result.get('definitions'), dict):
        definitions.pop('programs', None)
    return result


def merge_changes(
    setup: str,
    base: Value,
    host: Value,
    pedal: Value,
    path: list[str | int] | None = None,
) -> list[Change]:
    """Keep host-only edits; require review for ambiguous list movement."""
    path = path or []
    if pedal == base or pedal == host:
        return []
    if all(v.present and isinstance(v.value, dict) for v in (base, host, pedal)):
        b, h, p = (cast(dict[str, object], v.value) for v in (base, host, pedal))
        if ('parent' in b and 'parent' not in p) or (
            b.get('type') == 'program-change'
            and any(b.get(k) != p.get(k) for k in ('program', 'bank'))
        ):
            return [
                Change(
                    setup=setup,
                    path=path,
                    base=base,
                    host=host,
                    pedal=pedal,
                    choice=Choice.pedal if host == base else Choice.unresolved,
                    note='Materialize the complete program selection.',
                )
            ]
        return [
            c
            for k in sorted(b.keys() | h.keys() | p.keys())
            for c in merge_changes(
                setup, member(b, k), member(h, k), member(p, k), [*path, k]
            )
        ]
    if host == base and all(
        v.present and isinstance(v.value, list) for v in (base, pedal)
    ):
        b, p = cast(list[object], base.value), cast(list[object], pedal.value)
        if len(b) == len(p):
            return [
                c
                for i in range(len(b))
                for c in merge_changes(
                    setup,
                    Value(present=True, value=b[i]),
                    Value(present=True, value=b[i]),
                    Value(present=True, value=p[i]),
                    [*path, i],
                )
            ]
    return [
        Change(
            setup=setup,
            path=path,
            base=base,
            host=host,
            pedal=pedal,
            choice=Choice.pedal if host == base else Choice.unresolved,
        )
    ]


def member(value: dict[str, object], key: str) -> Value:
    return Value(present=key in value, value=value.get(key, ''))


def digest(data: bytes) -> str:
    return sha256(data).hexdigest()


def store(command: data_files.Sources) -> Path:
    expressions = data_files.expressions(command.document)
    if not expressions:
        raise ValueError('a root setup document is required')
    return expressions[0].paths[0].resolve().parent / 'setup-sync'


def recipe(command: data_files.Sources) -> data_files.Sources:
    def absolute(arguments: list[str]) -> list[str]:
        return [
            a
            for e in data_files.expressions(arguments)
            for a in [
                e.name.replace('=', '==')
                + '='
                + str(e.paths[0].resolve()).replace('=', '=='),
                *[
                    a
                    for p in e.paths[1:]
                    for a in ['+', str(p.resolve()).replace('=', '==')]
                ],
            ]
        ]

    return data_files.Sources(
        document=absolute(command.document),
        target=absolute(command.target),
        definitions=absolute(command.definitions),
        table=[absolute(t) for t in command.table],
        avoid_confict=command.avoid_confict,
        memory_margin=command.memory_margin,
    )


def write_record(path: Path, value: Mapping[str, object]) -> None:
    replace(path, tomlkit.dumps(value).encode())


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
    if (root / 'pending.toml').exists():
        raise ValueError(
            f'interrupted apply: cntrl setup sync --recover {root / "pending.toml"}'
        )


def base_record(root: Path) -> bytes:
    path = root / 'base.toml'
    return path.read_bytes() if path.exists() else b''


def baseline(root: Path) -> dict[str, bytes]:
    record = base_record(root)
    if not record:
        return {}
    files = tomlkit.parse(record.decode())['setups']
    result: dict[str, bytes] = {}
    for n, entry in files.items():
        data = (root / entry['file']).read_bytes()
        if digest(data) != entry['checksum']:
            raise ValueError(f'{n}: damaged synchronization baseline')
        result[n] = data
    return result


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
    record = tomlkit.parse(original.decode()).unwrap()
    entries = cast(dict[str, dict[str, str]], record['setups'])
    version = binary_format.format_name(binary_format.DEVELOPMENT_FORMAT)
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
        if destination.exists() and destination.read_bytes() != data:
            raise ValueError(f'{destination}: converted baseline checksum differs')
        replace(destination, data)
        entries[name] = {
            'file': str(destination.relative_to(root)),
            'checksum': digest(data),
        }
    write_record(root / 'base.toml', record)
    write_record(folder / 'applied.toml', {'complete': True})
    (root / 'pending.toml').unlink()


def advance(root: Path, folder: Path, names: list[str]) -> None:
    previous = (
        tomlkit.parse(base_record(root).decode()).unwrap()
        if base_record(root)
        else {'setups': {}}
    )
    setups = cast(dict[str, object], previous['setups'])
    for n in names:
        path = folder / f'{n}.bin'
        setups[n] = {
            'file': str(path.relative_to(root)),
            'checksum': digest(path.read_bytes()),
        }
    write_record(root / 'base.toml', previous)


def snapshot(
    root: Path,
    prefix: str,
    setups: dict[str, bytes],
    sources: data_files.Trace,
    inputs: data_files.Sources,
) -> Path:
    root.mkdir(exist_ok=True)
    folder = Path(mkdtemp(prefix=prefix, dir=root))
    records = []
    for i, (p, data) in enumerate(sources.files.items()):
        name = f'source-{i}.original'
        (folder / name).write_bytes(data)
        records.append(
            {
                'file': str(p),
                'snapshot': name,
                'checksum': digest(data),
                'consumers': sources.consumers.get(p, []),
            }
        )
    for n, data in setups.items():
        save_setup(folder, n, data)
    write_record(
        folder / 'snapshot.toml',
        {
            'document_version': compiler.DOCUMENT_VERSION,
            'compiler_checksum': digest(Path(compiler.__file__).read_bytes()),
            'recipe': recipe(inputs).model_dump(mode='json'),
            'sources': records,
        },
    )
    return folder


def save_setup(folder: Path, name: str, data: bytes) -> None:
    data_files.validate_name(name)
    (folder / f'{name}.bin').write_bytes(data)
    try:
        (folder / f'{name}.toml').write_bytes(resolved.export(data).encode())
    except ValueError as error:
        (folder / f'{name}.decode-error.txt').write_bytes(str(error).encode())


def capture_assets(
    client: transport.SetupClient, setups: dict[str, bytes], folder: Path
) -> None:
    fonts: set[int] = set()
    movies: set[int] = set()
    for data in setups.values():
        try:
            backups.backup_assets(client, data, folder, fonts, movies)
        except (OSError, ValueError) as error:
            raise ValueError(
                f'incomplete snapshot retained at {folder}: {error}'
            ) from error
    write_record(folder / 'complete.toml', {'complete': True})


def known_consumers(root: Path, trace: data_files.Trace) -> None:
    """Include source consumers retained with other known setup baselines."""
    if not base_record(root):
        return
    entries = tomlkit.parse(base_record(root).decode())['setups']
    for entry in entries.values():
        folder = (root / entry['file']).parent
        manifest = folder / 'snapshot.toml'
        if not manifest.exists():
            manifest = folder.parent / 'snapshot.toml'
        for source in tomlkit.parse(manifest.read_bytes().decode())['sources']:
            if (p := Path(source['file'])) in trace.files:
                consumers = trace.consumers.setdefault(p, [])
                consumers.extend(n for n in source['consumers'] if n not in consumers)


def destination(
    change: Change,
    trace: data_files.Trace,
    host: dict[str, object],
    pedal: dict[str, object],
) -> Change:
    path = change.path
    locations = trace.locations.get(change.setup, {})
    # Only the fields currently editable on the pedal may be placed automatically.
    editable = bool(path) and (
        path[0]
        in (
            'brightness',
            'screensaver_timeout',
            'is_midi_interface',
            'trs_ports',
            'auxiliary_switches',
        )
        or (path[0] == 'instruments' and path[-1] == 'channel')
        or (
            path[0] in ('patches', 'patch_cycles', 'switches')
            and (
                path[-1]
                in ('program', 'bank', 'msb', 'lsb', 'volume', 'pan', 'display_name')
                or change.note.startswith('Materialize')
            )
        )
    )
    if not editable:
        return change.model_copy(
            update={
                'choice': Choice.unresolved,
                'note': 'Manual placement required for this structural change.',
            }
        )
    # Missing defaults belong to the nearest authored table, including the root.
    for length in range(len(path), -1, -1):
        if (location := locations.get(tuple(path[:length]))) is None:
            continue
        if location.file.suffix != '.toml':
            break
        raw = tomlkit.parse(trace.files[location.file].decode()).unwrap()
        current = lookup(raw, location.path)
        suffix = path[length:]
        if suffix and not isinstance(current.value, dict):
            # An integer program or a derived parent that cannot express clearing
            # becomes the complete, authoritative program selection.
            if not path or path[0] not in ('patches', 'patch_cycles', 'switches'):
                break
            parent_path = path[:length]
            return placed(
                change,
                location,
                trace,
                'Materialize the complete program selection.',
                parent_path,
                host,
                pedal,
            )
        return placed(
            change,
            data_files.Location(file=location.file, path=[*location.path, *suffix]),
            trace,
            change.note,
        )
    return change.model_copy(
        update={
            'choice': Choice.unresolved,
            'note': 'No unambiguous TOML destination; place manually, then preview.',
        }
    )


def placed(
    change: Change,
    location: data_files.Location,
    trace: data_files.Trace,
    note: str,
    path: list[str | int] | None = None,
    host: dict[str, object] | None = None,
    pedal: dict[str, object] | None = None,
) -> Change:
    affects = trace.consumers.get(location.file, [])
    updates: dict[str, object] = {
        'destination': location,
        'affects': affects,
        'note': note,
    }
    if len(affects) > 1:
        updates['choice'] = Choice.unresolved
        updates['note'] = note + ' Shared source: explicitly choose host or pedal.'
    if path is not None:
        updates.update(
            path=path,
            host=lookup(host, path),
            pedal=lookup(pedal, path),
            choice=Choice.unresolved,
        )
    return change.model_copy(update=updates)


def lookup(value: object, path: list[str | int]) -> Value:
    for key in path:
        if isinstance(value, dict) and key in value:
            value = value[key]
        elif isinstance(value, list) and isinstance(key, int) and 0 <= key < len(value):
            value = value[key]
        else:
            return Value()
    return Value(present=True, value=value)


def assign(document: object, path: list[str | int], value: Value) -> None:
    current = document
    for key in path[:-1]:
        if isinstance(current, MutableMapping) and key not in current:
            current[key] = tomlkit.table()
        if isinstance(current, MutableMapping):
            current = current[key]
        elif isinstance(current, MutableSequence) and isinstance(key, int):
            current = current[key]
        else:
            raise ValueError(f'{path}: source shape requires manual placement')
    if not path:
        raise ValueError('cannot replace a whole source document automatically')
    key = path[-1]
    if isinstance(current, MutableMapping):
        if value.present:
            current[key] = deepcopy(value.value)
        elif key in current:
            del current[key]
    elif isinstance(current, MutableSequence) and isinstance(key, int):
        if value.present:
            current[key] = deepcopy(value.value)
        else:
            del current[key]
    else:
        raise ValueError(f'{path}: source shape requires manual placement')


def preview(inputs: data_files.Sources, client: transport.SetupClient) -> Path:
    from .cli import compile_inputs

    root = store(inputs)
    check_pending(root)
    migrate_baselines(root)
    trace = data_files.Trace()
    host_bundle = compile_inputs(inputs, trace=trace)
    known_consumers(root, trace)
    saved: dict[str, bytes] = {}
    folder = snapshot(root, 'review-', saved, trace, inputs)
    for n in host_bundle.names:
        saved[n] = client.download(n)
        save_setup(folder, n, saved[n])
    # Raw bytes are retained even if the decoder or asset download fails.
    capture_assets(client, saved, folder)
    bases = baseline(root)
    changes: list[Change] = []
    for n in host_bundle.names:
        host, pedal = canonical(host_bundle.setups[n]), canonical(saved[n])
        base = canonical(bases[n]) if n in bases else None
        if base is None:
            differences = merge_changes(
                n,
                Value(present=True, value=host),
                Value(present=True, value=host),
                Value(present=True, value=pedal),
            )
            differences = [
                c.model_copy(
                    update={
                        'choice': Choice.unresolved,
                        'base': Value(),
                        'note': c.note
                        + ' No baseline: explicitly choose host or pedal.',
                    }
                )
                for c in differences
            ]
        else:
            differences = merge_changes(
                n,
                Value(present=True, value=base),
                Value(present=True, value=host),
                Value(present=True, value=pedal),
            )
        for c in differences:
            located = destination(c, trace, host, pedal)
            if base is None:
                located = located.model_copy(
                    update={
                        'choice': Choice.unresolved,
                        'note': located.note
                        + ' No baseline: explicit choice required.',
                    }
                )
            changes.append(located)
    review = Review(
        root=root,
        recipe=recipe(inputs),
        baseline=digest(base_record(root)),
        sources=[Source(file=p, before=digest(v)) for p, v in trace.files.items()],
        host={n: digest(v) for n, v in host_bundle.setups.items()},
        pedal={n: digest(v) for n, v in saved.items()},
        changes=changes,
    )
    report = folder / 'review.toml'
    write_record(report, review.model_dump(mode='json', exclude_none=True))
    # A sealed copy fixes the compared values and destinations. Only choice is
    # editable in the review; Apply rechecks it against this captured proposal.
    (folder / 'proposal.toml').write_bytes(report.read_bytes())
    documents: dict[Path, MutableMapping[str, object]] = {}
    for c in changes:
        if c.destination is None:
            continue
        f = c.destination.file
        if f not in documents:
            documents[f] = tomlkit.parse(trace.files[f].decode())
        assign(documents[f], c.destination.path, c.pedal)
    diff = ''.join(
        s
        for f, d in documents.items()
        for s in unified_diff(
            trace.files[f].decode().splitlines(keepends=True),
            tomlkit.dumps(d).splitlines(keepends=True),
            fromfile=str(f),
            tofile=str(f),
        )
    )
    (folder / 'pedal-choices.diff').write_bytes(diff.encode())
    print(f'{len(changes)} changes; review {report}')
    for c in changes:
        print(f'{c.setup}: {c.path}: {c.choice}; {c.destination}; {c.note}')
    return report


def apply_review(path: Path) -> None:
    from .cli import compile_inputs

    path = path.resolve()
    review = Review.model_validate(tomlkit.parse(path.read_bytes().decode()).unwrap())
    proposal = Review.model_validate(
        tomlkit.parse((path.parent / 'proposal.toml').read_bytes().decode()).unwrap()
    )
    if (
        len(review.changes) != len(proposal.changes)
        or review.model_copy(
            update={
                'changes': [
                    c.model_copy(update={'choice': p.choice})
                    for c, p in zip(review.changes, proposal.changes, strict=True)
                ]
            }
        )
        != proposal
    ):
        raise ValueError(
            'only change choices in review.toml; preview again for other edits'
        )
    check_pending(review.root)
    if (path.parent / 'applied.toml').exists():
        raise ValueError('this review has already been applied; preview again')
    if any(c.choice == Choice.unresolved for c in review.changes):
        raise ValueError('resolve every choice to host or pedal before applying')
    if digest(base_record(review.root)) != review.baseline:
        raise ValueError('baseline changed since review; preview again')
    for n, checksum in review.pedal.items():
        if digest((path.parent / f'{n}.bin').read_bytes()) != checksum:
            raise ValueError(f'{n}: pedal snapshot changed since review; preview again')
    for s in review.sources:
        if digest(s.file.read_bytes()) != s.before:
            raise ValueError(f'{s.file}: source changed since review; preview again')
    trace = data_files.Trace()
    host = compile_inputs(review.recipe, trace=trace)
    if {n: digest(v) for n, v in host.setups.items()} != review.host:
        raise ValueError('compiled inputs changed since review; preview again')
    expected = {n: canonical(v) for n, v in host.setups.items()}
    documents: dict[Path, MutableMapping[str, object]] = {}
    for c in review.changes:
        if c.choice != Choice.pedal:
            continue
        if c.destination is None:
            raise ValueError(f'{c.setup}: {c.path}: manual source placement required')
        if any(n not in host.setups for n in c.affects):
            raise ValueError(
                f'{c.destination.file}: include every affected setup in a new '
                f'preview: {c.affects}'
            )
        f = c.destination.file
        if f not in documents:
            documents[f] = tomlkit.parse(f.read_bytes().decode())
        assign(documents[f], c.destination.path, c.pedal)
        assign(expected[c.setup], c.path, c.pedal)
    candidates = {p: tomlkit.dumps(v).encode() for p, v in documents.items()}
    compiled = compile_inputs(review.recipe, overrides=candidates)
    # Explicitly accepting a shared source edit accepts its same-field effects
    # on the listed consumers. No other candidate differences are accepted.
    for n in host.names:
        before, after = canonical(host.setups[n]), canonical(compiled.setups[n])
        for delta in merge_changes(
            n,
            Value(present=True, value=before),
            Value(present=True, value=before),
            Value(present=True, value=after),
        ):
            located = destination(delta, trace, before, after)
            if located.destination is None:
                continue
            if any(
                c.setup == n
                and (
                    delta.path[: len(c.path)] == c.path
                    or c.path[: len(delta.path)] == delta.path
                )
                for c in review.changes
            ):
                continue
            if any(
                c.choice == Choice.pedal
                and c.destination is not None
                and n in c.affects
                and c.destination.file == located.destination.file
                and located.destination.path[: len(c.destination.path)]
                == c.destination.path
                for c in review.changes
            ):
                assign(expected[n], delta.path, delta.pedal)
    if {n: canonical(v) for n, v in compiled.setups.items()} != expected:
        raise ValueError(
            'candidate compilation differs from the reviewed result; '
            'check shared or derived patches and preview again'
        )
    sources = []
    for i, (p, after) in enumerate(candidates.items()):
        before = p.read_bytes()
        if before == after:
            continue
        (path.parent / f'apply-{i}.before').write_bytes(before)
        (path.parent / f'apply-{i}.after').write_bytes(after)
        sources.append(
            {'file': str(p), 'before': f'apply-{i}.before', 'after': f'apply-{i}.after'}
        )
    journal = {'review': str(path), 'sources': sources}
    # Check again immediately before the first replacement, after compilation.
    for s in review.sources:
        if digest(s.file.read_bytes()) != s.before:
            raise ValueError(f'{s.file}: source changed during apply')
    if digest(base_record(review.root)) != review.baseline:
        raise ValueError('baseline changed during apply')
    before_base = base_record(review.root)
    (path.parent / 'base.before').write_bytes(before_base)
    write_record(review.root / 'pending.toml', journal)
    for s in sources:
        replace(Path(s['file']), (path.parent / s['after']).read_bytes())
    advance(review.root, path.parent, list(review.host))
    write_record(path.parent / 'applied.toml', {'complete': True})
    (review.root / 'pending.toml').unlink()
    print(
        f'Applied {len(sources)} source files. '
        'Baseline is the captured pedal state. No upload performed.'
    )


def recover(path: Path) -> None:
    journal = tomlkit.parse(path.read_bytes().decode()).unwrap()
    folder = Path(journal['review']).parent
    # Refuse to discard edits made after an interrupted publication.
    for s in journal['sources']:
        current = Path(s['file']).read_bytes()
        if current not in (
            (folder / s['before']).read_bytes(),
            (folder / s['after']).read_bytes(),
        ):
            raise ValueError(
                f'{s["file"]}: changed after interruption; restore manually'
            )
    for s in journal['sources']:
        replace(Path(s['file']), (folder / s['before']).read_bytes())
    before = (folder / 'base.before').read_bytes()
    if before:
        replace(path.parent / 'base.toml', before)
    else:
        (path.parent / 'base.toml').unlink(missing_ok=True)
    (folder / 'applied.toml').unlink(missing_ok=True)
    path.unlink()
    print('Restored original sources and baseline. Preview again before applying.')


def run(command: Sync) -> None:
    if command.apply is not None and command.recover is not None:
        raise ValueError('choose either --apply or --recover')
    if (command.apply is not None or command.recover is not None) and (
        command.document or command.target or command.port is not None
    ):
        raise ValueError(
            '--apply and --recover use stored inputs; omit documents, target and port'
        )
    if command.recover is not None:
        recover(command.recover)
    elif command.apply is not None:
        apply_review(command.apply)
    else:
        if command.port is None:
            raise ValueError('--port is required for preview')
        with transport.SerialConnection(command.port) as connection:
            preview(command, transport.SetupClient(connection))


def prepare_upload(
    inputs: data_files.Sources,
    bundle: setup_bundle.SetupBundle,
    trace: data_files.Trace,
    client: transport.SetupClient,
) -> tuple[setup_bundle.SetupBundle, Path]:
    """Archive all current setups and retain independent, unreplaced pedal data."""
    current_bundle = client.download_bundle()
    root = store(inputs)
    check_pending(root)
    migrate_baselines(root)
    existing: dict[str, bytes] = {}
    folder = snapshot(root, 'upload-', existing, trace, inputs)
    if current_bundle:
        (folder / 'bundle.before').write_bytes(current_bundle)
    for e in client.catalogue():
        existing[e.name] = client.download(e.name)
        save_setup(folder, e.name, existing[e.name])
    capture_assets(client, existing, folder)
    bases = baseline(root)
    prepared = dict(bundle.setups)
    for n in bundle.names:
        if n in existing and existing[n].startswith(binary_format.MAGIC):
            document = binary_format.unpack(existing[n], binary_format.Kind.setup)
            if len(document.records) > 1:
                if binary_format.core(existing[n]) != binary_format.core(prepared[n]):
                    raise ValueError(
                        f'{n}: cannot recompile opaque metadata. Backup: {folder}'
                    )
                prepared[n] = existing[n]
        if (
            n in existing
            and n in bases
            and canonical(existing[n]) != canonical(bases[n])
        ):
            raise ValueError(
                f'{n}: unreconciled pedal edits; run cntrl setup sync first. '
                f'Backup: {folder}'
            )
        if (
            n in existing
            and n not in bases
            and canonical(existing[n]) != canonical(bundle.setups[n])
        ):
            raise ValueError(
                f'{n}: no synchronization baseline; run cntrl setup sync and '
                f'review the differences before uploading. Backup: {folder}'
            )
    combined = setup_bundle.reindex(existing | prepared)
    sources: dict[str, object] = {
        n: tomlkit.parse(resolved.export(v)).unwrap() for n, v in combined.items()
    }
    result = setup_bundle.build(
        combined,
        sources,
        bundle.memory,
        bundle.memory_margin,
        previous=current_bundle or None,
    )
    uploaded = folder / 'uploaded'
    uploaded.mkdir()
    for n in bundle.names:
        (uploaded / f'{n}.bin').write_bytes(result.setups[n])
        (uploaded / f'{n}.toml').write_bytes(resolved.export(result.setups[n]).encode())
    return result, uploaded


def check_upload(client: transport.SetupClient, folder: Path) -> None:
    """Refuse to replace a collection that changed while preparing an upload."""
    paths = {p.stem: p for p in folder.glob('*.bin')}
    if set(paths) != {e.name for e in client.catalogue()}:
        raise ValueError('pedal collection changed during upload preparation')
    for n, path in paths.items():
        if client.download(n) != path.read_bytes():
            raise ValueError(f'{n}: pedal setup changed during upload preparation')
