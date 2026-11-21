"""Reviewed three-way imports into the original setup authoring documents."""

from collections.abc import MutableMapping, MutableSequence
from copy import deepcopy
from difflib import unified_diff
from pathlib import Path
from tempfile import mkdtemp
from typing import Annotated, cast

import tomlkit
import tyro
from pydantic import Field

from ..file_lock import writer_lock
from . import (
    backups,
    binary_format,
    compiler,
    data_files,
    device_globals,
    device_settings,
    global_config,
    global_overlay,
    models,
    resolved,
    setup_bundle,
    source_archive,
    transport,
)
from .setup_compiler import compile_inputs
from .setup_sync_store import (
    Change,
    Choice,
    Review,
    Source,
    Value,
    base_record,
    baseline,
    check_pending,
    digest,
    global_baseline,
    migrate_baselines,
    publish,
    recover,
    write_record,
)


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


def canonical(data: bytes) -> dict[str, object]:
    """Decode authoritative MIDI values, excluding stale catalogue annotations."""
    result = tomlkit.parse(resolved.export(data)).unwrap()
    # Program catalogue annotations describe the authored selection, not edits
    # saved into the compiled graph. Profiles still carry actual bank mappings.
    if isinstance(definitions := result.get('definitions'), dict):
        definitions.pop('programs', None)
    return result


def same_saved_setup(left: bytes, right: bytes) -> bool:
    before = device_settings.override_mask(left)
    after = device_settings.override_mask(right)
    return canonical(left) == canonical(right) and (
        0xFFFFFFFFFFFFFFFF in (before, after) or before == after
    )


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


def store(command: data_files.Sources) -> Path:
    expressions = data_files.expressions(command.document)
    if not expressions:
        raise ValueError('a root setup document is required')
    return expressions[0].paths[0].resolve().parent / 'setup-sync'


def recipe(
    command: data_files.Sources, trace: data_files.Trace | None = None
) -> data_files.Sources:
    command = global_config.resolve_inputs(command, trace=trace)

    def absolute(arguments: list[str]) -> list[str]:
        return [
            a
            for e in data_files.expressions(arguments)
            for a in global_config.expression_arguments(
                data_files.DataFile(name=e.name, paths=[p.resolve() for p in e.paths])
            )
        ]

    return data_files.Sources(
        document=absolute(command.document),
        target=absolute(command.target),
        definitions=absolute(command.definitions),
        table=[absolute(t) for t in command.table],
        avoid_confict=command.avoid_confict,
        memory_margin=command.memory_margin,
        global_defaults=(
            trace.defaults
            if trace is not None and trace.defaults
            else command.global_defaults
        ),
    )


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
            'recipe': recipe(inputs, sources).model_dump(
                mode='json', exclude_none=True
            ),
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
    if change.setting_override == 'inherit':
        location = locations.get(tuple(path))
        if location is None or location.file in trace.defaults.values():
            return change.model_copy(
                update={
                    'note': 'Restore setting inheritance by removing '
                    'the local declaration manually.'
                }
            )
        return placed(
            change,
            location,
            trace,
            'Remove this setup-local setting override; retain global defaults.',
        )
    if (inherited := inherited_destination(change, trace, host, pedal)) is not None:
        return inherited
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


def inherited_destination(
    change: Change,
    trace: data_files.Trace,
    host: dict[str, object],
    pedal: dict[str, object],
) -> Change | None:
    """Keep inherited edits local, replacing whole inherited lists when necessary."""
    inherited = trace.inherited.get(change.setup, {})
    root = trace.roots.get(change.setup)
    path = change.path
    if root is None or not path or not inherited:
        return None
    locations = trace.locations.get(change.setup, {})
    inherited_path: tuple[str | int, ...] | None = None
    materialize = change.note.startswith('Materialize')
    for length in range(len(path), 0, -1):
        prefix = tuple(path[:length])
        if prefix not in inherited:
            continue
        source = locations.get(prefix)
        if source == inherited[prefix] or not change.pedal.present or materialize:
            inherited_path = prefix
            break
        if source is not None:
            return None
    if inherited_path is None:
        return None
    if root.suffix != '.toml':
        return change.model_copy(
            update={
                'choice': Choice.unresolved,
                'note': 'An inherited edit requires a setup-local TOML source.',
            }
        )
    # An inherited list is replaced by the setup layer. Its changed element
    # cannot be written alone without dropping the unchanged elements.
    for index, key in enumerate(path):
        if isinstance(key, int):
            path = path[:index]
            change = change.model_copy(
                update={
                    'path': path,
                    'host': lookup(host, path),
                    'pedal': lookup(pedal, path),
                }
            )
            break
    source = inherited[inherited_path]
    raw = tomlkit.parse(trace.files[source.file].decode()).unwrap()
    value = lookup(raw, source.path)
    if len(inherited_path) < len(path) and not isinstance(value.value, dict):
        path = list(inherited_path)
        change = change.model_copy(
            update={
                'path': path,
                'host': lookup(host, path),
                'pedal': lookup(pedal, path),
                'choice': Choice.unresolved,
                'note': 'Materialize the complete inherited definition locally.',
            }
        )
    elif (
        not change.pedal.present
        and not lookup(value.value, path[len(inherited_path) :]).present
    ):
        # Removing a setup-only field must not create a removal for a value
        # that never existed in global defaults.
        return None
    result = placed(
        change,
        data_files.Location(file=root, path=path),
        trace,
        change.note + ' Override global defaults in this setup only.',
    )
    return result.model_copy(
        update={
            'remove_inherited': not result.pedal.present
            or (
                result.note.startswith('Materialize')
                and isinstance(result.pedal.value, dict)
            )
        }
    )


def apply_change(document: MutableMapping[str, object], change: Change) -> None:
    if change.destination is None:
        raise ValueError(
            f'{change.setup}: {change.path}: manual source placement required'
        )
    if change.scope == 'device-global':
        document['format'] = 2
        assign(document, change.destination.path, change.pedal)
        return
    if change.remove_inherited:
        if any(not isinstance(p, str) for p in change.path):
            raise ValueError('inherited removals require table keys')
        current = document.setdefault('inheritance', tomlkit.table())
        if not isinstance(current, MutableMapping):
            raise ValueError('inheritance must be a table')
        removals = current.setdefault('remove', [])
        if not isinstance(removals, MutableSequence):
            raise ValueError('inheritance.remove must be a list')
        path = change.path
        if not any(list(p) == path[: len(p)] for p in removals):
            for index in reversed(range(len(removals))):
                if list(removals[index])[: len(path)] == path:
                    del removals[index]
            removals.append(path)
    assign(document, change.destination.path, change.pedal)


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
    root = store(inputs)
    with writer_lock(root, operation='setup sync operation'):
        return preview_locked(inputs, client, root)


def preview_locked(
    inputs: data_files.Sources, client: transport.SetupClient, root: Path
) -> Path:
    from .source_archive import recovered_baselines

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
    bases = recovered_baselines(root.parent) | baseline(root)
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
            mask_changes = setting_changes(
                n, bases[n], host_bundle.setups[n], saved[n], inputs, trace
            )
            paths = {tuple(c.path) for c in mask_changes}
            differences = [
                c for c in differences if tuple(c.path) not in paths
            ] + mask_changes
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
    live_globals = device_globals.capture(client)
    global_checksum: str | None = None
    overlay = root.parent / 'device-globals.toml'
    overlay_present = overlay.exists()
    if live_globals is not None:
        captured = device_globals.dump(live_globals)
        (folder / 'device-globals.toml').write_bytes(captured)
        global_checksum = digest(captured)
        overlay_bytes = overlay.read_bytes() if overlay_present else b''
        host_globals = (
            device_globals.load(overlay_bytes)
            if overlay_present
            else global_overlay.Overlay()
        )
        base_globals = (
            global_baseline(root) if overlay_present else global_overlay.Overlay()
        )
        base_values = base_globals if base_globals is not None else host_globals
        for field in sorted(
            base_values.values.keys()
            | host_globals.values.keys()
            | live_globals.values.keys()
        ):
            key = str(field)
            path: list[str | int] = ['values', key]
            base_value = Value(
                present=field in base_values.values,
                value=base_values.values.get(field, ''),
            )
            host_value = Value(
                present=field in host_globals.values,
                value=host_globals.values.get(field, ''),
            )
            pedal_value = Value(
                present=field in live_globals.values,
                value=live_globals.values.get(field, ''),
            )
            for change in merge_changes(
                'device-global', base_value, host_value, pedal_value, path
            ):
                affects = [
                    n
                    for n, d in host_bundle.setups.items()
                    if not device_settings.override_mask(d) & (1 << field)
                ]
                setting_name = device_settings.definition(field).name
                changes.append(
                    change.model_copy(
                        update={
                            'scope': 'device-global',
                            'destination': data_files.Location(file=overlay, path=path),
                            'affects': affects,
                            'choice': Choice.unresolved
                            if base_globals is None
                            else change.choice,
                            'note': f'Device-global {setting_name}; '
                            'preserve compiled setup defaults and local overrides.',
                        }
                    )
                )
        for change in merge_changes(
            'device-global',
            Value(
                present=base_values.language is not None,
                value=base_values.language or '',
            ),
            Value(
                present=host_globals.language is not None,
                value=host_globals.language or '',
            ),
            Value(
                present=live_globals.language is not None,
                value=live_globals.language or '',
            ),
            ['language'],
        ):
            changes.append(
                change.model_copy(
                    update={
                        'scope': 'device-global',
                        'destination': data_files.Location(
                            file=overlay, path=['language']
                        ),
                        'affects': list(host_bundle.names),
                        'choice': Choice.unresolved
                        if base_globals is None
                        else change.choice,
                        'note': 'Device-wide UI language; '
                        'preserve compiled setup defaults.',
                    }
                )
            )
        trace.files[overlay] = overlay_bytes
    review = Review(
        root=root,
        recipe=recipe(inputs, trace),
        baseline=digest(base_record(root)),
        sources=[
            Source(file=p, before=digest(v), present=p.exists())
            for p, v in trace.files.items()
        ],
        host={n: digest(v) for n, v in host_bundle.setups.items()},
        pedal={n: digest(v) for n, v in saved.items()},
        changes=changes,
        global_snapshot=global_checksum,
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
        apply_change(documents[f], c)
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


def setting_changes(
    name: str,
    base: bytes,
    host: bytes,
    pedal: bytes,
    inputs: data_files.Sources,
    trace: data_files.Trace,
) -> list[Change]:
    if any(
        not d.startswith(binary_format.MAGIC)
        or not any(
            r.tag == binary_format.SETTING_OVERRIDES
            for r in binary_format.unpack(d, binary_format.Kind.setup).records
        )
        for d in (base, pedal)
    ):
        return []
    before = device_settings.override_mask(base)
    current = device_settings.override_mask(pedal)
    authored = device_settings.override_mask(host)
    changed = (before ^ current) & device_settings.KNOWN_MASK
    if not changed:
        return []
    recorded = recipe(inputs, trace)
    roots = data_files.expressions(recorded.document)
    _, relocations = source_archive.session_context(roots[0], snapshots=trace.files)
    errors: list[str] = []
    targets = data_files.expressions(recorded.target)
    target = models.Target.model_validate(
        data_files.load_target(
            targets[0],
            recorded.avoid_confict,
            errors,
            overrides=trace.files,
            relocations=relocations,
        )
    )
    if errors:
        raise ValueError('\n'.join(errors))
    values = [
        models.Setup.model_validate(canonical(d)).model_dump(mode='json')
        for d in (base, host, pedal)
    ]
    result: list[Change] = []
    for field in range(device_settings.COUNT):
        bit = 1 << field
        if not changed & bit:
            continue
        if current & bit and authored & bit:
            continue
        try:
            path: list[str | int] = list(device_settings.field_path(field, target))
        except ValueError:
            continue
        b, h, p = (lookup(v, path) for v in values)
        if not p.present:
            p = Value(
                present=True, value=device_settings.decode_value(field, 0, target)
            )
        if field == 10 and p.value is None:
            p = Value(present=True, value='none')
        explicit = bool(current & bit)
        choice = (
            Choice.pedal
            if bool(authored & bit) == bool(before & bit)
            and (not before & bit or h == b)
            else Choice.unresolved
        )
        if not explicit and not authored & bit:
            choice = Choice.host
        result.append(
            Change(
                setup=name,
                path=path,
                base=b,
                host=h,
                pedal=p if explicit else Value(),
                choice=choice,
                setting_override='set' if explicit else 'inherit',
                setting_field=field,
                note='Preserve the pedal setting inheritance choice.',
            )
        )
    return result


def apply_review(path: Path) -> None:
    path = path.resolve()
    review = Review.model_validate(tomlkit.parse(path.read_bytes().decode()).unwrap())
    with writer_lock(review.root, operation='setup sync operation'):
        apply_locked(path, review)


def apply_locked(path: Path, review: Review) -> None:
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
    if (
        review.global_snapshot is not None
        and digest((path.parent / 'device-globals.toml').read_bytes())
        != review.global_snapshot
    ):
        raise ValueError('device-global snapshot changed since review; preview again')
    for s in review.sources:
        if (
            s.file.exists() != s.present
            or digest(s.file.read_bytes() if s.present else b'') != s.before
        ):
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
        if c.scope != 'device-global' and any(n not in host.setups for n in c.affects):
            raise ValueError(
                f'{c.destination.file}: include every affected setup in a new '
                f'preview: {c.affects}'
            )
        f = c.destination.file
        if f not in documents:
            documents[f] = (
                tomlkit.parse(f.read_bytes().decode())
                if f.exists()
                else tomlkit.document()
            )
        apply_change(documents[f], c)
        if c.scope != 'device-global' and c.setting_override != 'inherit':
            assign(expected[c.setup], c.path, c.pedal)
    candidates = {p: tomlkit.dumps(v).encode() for p, v in documents.items()}
    for change in review.changes:
        if (
            change.scope == 'device-global'
            and change.destination is not None
            and change.destination.file in candidates
        ):
            device_globals.load(candidates[change.destination.file])
    compiled = compile_inputs(review.recipe, overrides=candidates)
    for change in review.changes:
        if change.choice != Choice.pedal or change.setting_field is None:
            continue
        mask = device_settings.override_mask(compiled.setups[change.setup])
        if bool(mask & (1 << change.setting_field)) != (
            change.setting_override == 'set'
        ):
            raise ValueError(
                'candidate setting inheritance differs from the reviewed result'
            )
        if change.setting_override == 'inherit':
            assign(
                expected[change.setup],
                change.path,
                lookup(canonical(compiled.setups[change.setup]), change.path),
            )
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
    published = publish(path, review, candidates)
    print(
        f'Applied {published} source files. '
        'Baseline is the captured pedal state. No upload performed.'
    )


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
            if binary_format.has_opaque(existing[n]):
                if binary_format.core(existing[n]) != binary_format.core(prepared[n]):
                    raise ValueError(
                        f'{n}: cannot recompile opaque metadata. Backup: {folder}'
                    )
                prepared[n] = existing[n]
        if n in existing and n in bases and not same_saved_setup(existing[n], bases[n]):
            raise ValueError(
                f'{n}: unreconciled pedal edits; run cntrl setup sync first. '
                f'Backup: {folder}'
            )
        if (
            n in existing
            and n not in bases
            and not same_saved_setup(existing[n], bundle.setups[n])
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
