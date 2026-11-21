"""Reviewed three-way imports into the original setup authoring documents."""

from collections.abc import MutableMapping
from difflib import unified_diff
from pathlib import Path
from tempfile import mkdtemp
from typing import Annotated, cast

import tomlkit
import tyro
from pydantic import Field, ValidationError
from tomlkit.exceptions import ParseError

from ..file_lock import writer_lock
from . import (
    backups,
    binary_format,
    compiler,
    compiler_errors,
    compiler_inputs,
    compiler_wire,
    data_files,
    device_globals,
    device_settings,
    global_config,
    global_overlay,
    models,
    patch_graph,
    resolved,
    setup_bundle,
    source_archive,
    transport,
)
from .setup_compiler import compile_inputs
from .setup_sync_sources import (
    apply_change,
    assign,
    destination,
    known_consumers,
    lookup,
)
from .setup_sync_store import (
    Change,
    Choice,
    Review,
    Source,
    SyncError,
    SyncFileError,
    Value,
    base_record,
    baseline,
    check_pending,
    digest,
    global_baseline,
    migrate_baselines,
    parse_record,
    publish,
    read_review,
    read_sync_file,
    recover,
    sync_files,
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
        raise SyncError('a root setup document is required')
    with sync_files():
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
    with sync_files():
        root.mkdir(exist_ok=True)
        folder = Path(mkdtemp(prefix=prefix, dir=root))
    records = []
    for i, (p, data) in enumerate(sources.files.items()):
        name = f'source-{i}.original'
        with sync_files():
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
            'document_version': compiler_wire.DOCUMENT_VERSION,
            'compiler_checksum': digest(
                ''.join(
                    digest(read_sync_file(Path(m.__file__)))
                    for m in (
                        compiler,
                        compiler_inputs,
                        compiler_errors,
                        compiler_wire,
                        patch_graph,
                    )
                ).encode('ascii')
            ),
            'recipe': recipe(inputs, sources).model_dump(
                mode='json', exclude_none=True
            ),
            'sources': records,
        },
    )
    return folder


def save_setup(folder: Path, name: str, data: bytes) -> None:
    data_files.validate_name(name)
    with sync_files():
        (folder / f'{name}.bin').write_bytes(data)
    try:
        text = resolved.export(data)
    except binary_format.DocumentFormatError as error:
        with sync_files():
            (folder / f'{name}.decode-error.txt').write_bytes(str(error).encode())
    else:
        with sync_files():
            (folder / f'{name}.toml').write_bytes(text.encode())


def capture_assets(
    client: transport.SetupClient, setups: dict[str, bytes], folder: Path
) -> None:
    from .cli import CompileOutputError

    fonts: set[int] = set()
    movies: set[int] = set()
    for data in setups.values():
        try:
            backups.backup_assets(client, data, folder, fonts, movies)
        except (
            *transport.USB_ERRORS,
            transport.UsbTransferInputError,
            binary_format.DocumentFormatError,
            CompileOutputError,
            SyncFileError,
        ) as error:
            raise SyncError(
                f'incomplete snapshot retained at {folder}: {error}'
            ) from error
    write_record(folder / 'complete.toml', {'complete': True})


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
    with sync_files():
        overlay_present = overlay.exists()
    if live_globals is not None:
        captured = device_globals.dump(live_globals)
        with sync_files():
            (folder / 'device-globals.toml').write_bytes(captured)
        global_checksum = digest(captured)
        overlay_bytes = read_sync_file(overlay) if overlay_present else b''
        host_globals = (
            device_globals.load_saved(overlay, overlay_bytes)
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
    with sync_files():
        sources = [
            Source(file=p, before=digest(v), present=p.exists())
            for p, v in trace.files.items()
        ]
    review = Review(
        root=root,
        recipe=recipe(inputs, trace),
        baseline=digest(base_record(root)),
        sources=sources,
        host={n: digest(v) for n, v in host_bundle.setups.items()},
        pedal={n: digest(v) for n, v in saved.items()},
        changes=changes,
        global_snapshot=global_checksum,
    )
    report = folder / 'review.toml'
    write_record(report, review.model_dump(mode='json', exclude_none=True))
    # A sealed copy fixes the compared values and destinations. Only choice is
    # editable in the review; Apply rechecks it against this captured proposal.
    with sync_files():
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
    with sync_files():
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
        raise SyncError('\n'.join(errors))
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
        except device_settings.DeviceSettingError:
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
    with sync_files():
        path = path.resolve()
    review = read_review(path)
    with writer_lock(review.root, operation='setup sync operation'):
        apply_locked(path, review)


def apply_locked(path: Path, review: Review) -> None:
    validate_review(path, review)
    published = publish(path, review, compile_review(review))
    print(
        f'Applied {published} source files. '
        'Baseline is the captured pedal state. No upload performed.'
    )


def validate_review(path: Path, review: Review) -> None:
    proposal = read_review(path.parent / 'proposal.toml')
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
        raise SyncError(
            'only change choices in review.toml; preview again for other edits'
        )
    check_pending(review.root)
    with sync_files():
        applied = (path.parent / 'applied.toml').exists()
    if applied:
        raise SyncError('this review has already been applied; preview again')
    if any(c.choice == Choice.unresolved for c in review.changes):
        raise SyncError('resolve every choice to host or pedal before applying')
    if digest(base_record(review.root)) != review.baseline:
        raise SyncError('baseline changed since review; preview again')
    for n, checksum in review.pedal.items():
        if digest(read_sync_file(path.parent / f'{n}.bin')) != checksum:
            raise SyncError(f'{n}: pedal snapshot changed since review; preview again')
    if (
        review.global_snapshot is not None
        and digest(read_sync_file(path.parent / 'device-globals.toml'))
        != review.global_snapshot
    ):
        raise SyncError('device-global snapshot changed since review; preview again')
    for s in review.sources:
        with sync_files():
            present = s.file.exists()
        if (
            present != s.present
            or digest(read_sync_file(s.file) if s.present else b'') != s.before
        ):
            raise SyncError(f'{s.file}: source changed since review; preview again')


def compile_review(review: Review) -> dict[Path, bytes]:
    trace = data_files.Trace()
    host = compile_inputs(review.recipe, trace=trace)
    if {n: digest(v) for n, v in host.setups.items()} != review.host:
        raise SyncError('compiled inputs changed since review; preview again')
    expected = {n: canonical(v) for n, v in host.setups.items()}
    documents: dict[Path, MutableMapping[str, object]] = {}
    for c in review.changes:
        if c.choice != Choice.pedal:
            continue
        if c.destination is None:
            raise SyncError(f'{c.setup}: {c.path}: manual source placement required')
        if c.scope != 'device-global' and any(n not in host.setups for n in c.affects):
            raise SyncError(
                f'{c.destination.file}: include every affected setup in a new '
                f'preview: {c.affects}'
            )
        f = c.destination.file
        if f not in documents:
            with sync_files():
                present = f.exists()
            documents[f] = (
                parse_record(f, read_sync_file(f)) if present else tomlkit.document()
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
            try:
                device_globals.load(candidates[change.destination.file])
            except (ParseError, UnicodeDecodeError, ValidationError) as error:
                raise SyncError(f'{change.destination.file}: {error}') from error
    compiled = compile_inputs(review.recipe, overrides=candidates)
    for change in review.changes:
        if change.choice != Choice.pedal or change.setting_field is None:
            continue
        mask = device_settings.override_mask(compiled.setups[change.setup])
        if bool(mask & (1 << change.setting_field)) != (
            change.setting_override == 'set'
        ):
            raise SyncError(
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
        raise SyncError(
            'candidate compilation differs from the reviewed result; '
            'check shared or derived patches and preview again'
        )
    return candidates


def run(command: Sync) -> None:
    if command.apply is not None and command.recover is not None:
        raise SyncError('choose either --apply or --recover')
    if (command.apply is not None or command.recover is not None) and (
        command.document or command.target or command.port is not None
    ):
        raise SyncError(
            '--apply and --recover use stored inputs; omit documents, target and port'
        )
    if command.recover is not None:
        recover(command.recover)
    elif command.apply is not None:
        apply_review(command.apply)
    else:
        if command.port is None:
            raise SyncError('--port is required for preview')
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
        with sync_files():
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
                    raise SyncError(
                        f'{n}: cannot recompile opaque metadata. Backup: {folder}'
                    )
                prepared[n] = existing[n]
        if n in existing and n in bases and not same_saved_setup(existing[n], bases[n]):
            raise SyncError(
                f'{n}: unreconciled pedal edits; run cntrl setup sync first. '
                f'Backup: {folder}'
            )
        if (
            n in existing
            and n not in bases
            and not same_saved_setup(existing[n], bundle.setups[n])
        ):
            raise SyncError(
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
    with sync_files():
        uploaded.mkdir()
    for n in bundle.names:
        data = result.setups[n]
        with sync_files():
            (uploaded / f'{n}.bin').write_bytes(data)
        text = resolved.export(data)
        with sync_files():
            (uploaded / f'{n}.toml').write_bytes(text.encode())
    return result, uploaded


def check_upload(client: transport.SetupClient, folder: Path) -> None:
    """Refuse to replace a collection that changed while preparing an upload."""
    with sync_files():
        paths = {p.stem: p for p in folder.glob('*.bin')}
    if set(paths) != {e.name for e in client.catalogue()}:
        raise SyncError('pedal collection changed during upload preparation')
    for n, path in paths.items():
        if client.download(n) != read_sync_file(path):
            raise SyncError(f'{n}: pedal setup changed during upload preparation')
