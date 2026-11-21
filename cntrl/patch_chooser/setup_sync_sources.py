"""Locate and apply reviewed pedal changes in their authored TOML files."""

from collections.abc import MutableMapping, MutableSequence
from copy import deepcopy
from pathlib import Path

import tomlkit

from . import data_files
from .setup_sync_store import Change, Choice, SyncError, Value, base_record


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
        raise SyncError(
            f'{change.setup}: {change.path}: manual source placement required'
        )
    if change.scope == 'device-global':
        document['format'] = 2
        assign(document, change.destination.path, change.pedal)
        return
    if change.remove_inherited:
        if any(not isinstance(p, str) for p in change.path):
            raise SyncError('inherited removals require table keys')
        current = document.setdefault('inheritance', tomlkit.table())
        if not isinstance(current, MutableMapping):
            raise SyncError('inheritance must be a table')
        removals = current.setdefault('remove', [])
        if not isinstance(removals, MutableSequence):
            raise SyncError('inheritance.remove must be a list')
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
            raise SyncError(f'{path}: source shape requires manual placement')
    if not path:
        raise SyncError('cannot replace a whole source document automatically')
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
        raise SyncError(f'{path}: source shape requires manual placement')
