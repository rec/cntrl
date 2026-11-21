"""Resolve complete setup defaults before validation fills omitted values."""

import os
import sys
from copy import deepcopy
from pathlib import Path

from . import compiler_errors, compiler_inputs, data_files


def user_path() -> Path:
    if sys.platform == 'darwin':
        root = Path.home() / 'Library' / 'Application Support'
    elif sys.platform == 'win32':
        root = Path(os.environ.get('APPDATA') or Path.home() / 'AppData' / 'Roaming')
    else:
        root = Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config')
    return root / 'cntrl' / 'global.toml'


def resolve_inputs(
    command: data_files.Sources,
    *,
    trace: data_files.Trace | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> data_files.Sources:
    """Recover a session root's recorded source inputs without new CLI flags."""
    from .source_archive import recovered_recipe, session_context

    snapshots = trace.files if trace is not None else None
    documents: list[data_files.DataFile] = []
    recipes: list[data_files.Sources] = []
    bindings: dict[str, Path | None] = {}
    for expression in data_files.expressions(command.document):
        selected, relocations = session_context(
            expression, snapshots=snapshots, overrides=overrides
        )
        if not relocations:
            documents.append(expression)
            continue
        recorded = recovered_recipe(
            expression.paths[0].resolve().parent,
            snapshots=snapshots,
            overrides=overrides,
        )
        candidates = [
            e
            for e in data_files.expressions(recorded.document)
            if e.paths[0].resolve() == expression.paths[0].resolve()
        ]
        exact = [e for e in candidates if e.name == expression.name]
        if len(exact or candidates) != 1:
            raise compiler_errors.CompilationError(
                [f'{expression.paths[0]}: ambiguous or missing recorded root setup']
            )
        chosen = (exact or candidates)[0]
        documents.append(
            data_files.DataFile(
                name=expression.name,
                paths=chosen.paths if len(expression.paths) == 1 else expression.paths,
            )
        )
        recipes.append(recorded)
        bindings.update(selected)
    if not recipes:
        return command
    updates: dict[str, object] = {
        'document': [a for e in documents for a in expression_arguments(e)],
        'global_defaults': bindings | (command.global_defaults or {}),
    }
    for field in ('target', 'definitions', 'table'):
        if getattr(command, field):
            continue
        candidates = [getattr(r, field) for r in recipes]
        if any(c != candidates[0] for c in candidates[1:]):
            raise compiler_errors.CompilationError(
                [
                    f'recovered setups use different {field} sources; '
                    'specify shared inputs'
                ]
            )
        updates[field] = candidates[0]
    for field in ('avoid_confict', 'memory_margin'):
        if field not in command.model_fields_set:
            candidates = [getattr(r, field) for r in recipes]
            if any(c != candidates[0] for c in candidates[1:]):
                raise compiler_errors.CompilationError(
                    [f'recovered setups use different {field}; specify a shared value']
                )
            updates[field] = candidates[0]
    return command.model_copy(update=updates)


def expression_arguments(expression: data_files.DataFile) -> list[str]:
    result: list[str] = []
    for i, path in enumerate(expression.paths):
        if i:
            result.append('+')
            result.append(str(path).replace('=', r'\='))
        else:
            result.append(expression.name.replace('=', '==') + '=' + str(path))
    return result


def load_setup(
    expression: data_files.DataFile,
    avoid_confict: bool,
    errors: list[str],
    *,
    trace: data_files.Trace | None = None,
    overrides: dict[Path, bytes] | None = None,
    defaults: dict[str, Path | None] | None = None,
) -> dict[str, object]:
    from .source_archive import session_context

    if trace is None:
        trace = data_files.Trace()
    try:
        bindings, relocations = session_context(
            expression,
            snapshots=trace.files if trace is not None else None,
            overrides=overrides,
        )
    except (OSError, ValueError) as error:
        errors.append(f'{expression.paths[0]}: {error}')
        return {}
    value = data_files.load(
        expression,
        avoid_confict,
        errors,
        trace=trace,
        overrides=overrides,
        defer_references=True,
        relocations=relocations,
    )
    return resolve_setup(
        value,
        expression,
        errors,
        trace=trace,
        overrides=overrides,
        defaults=bindings | (defaults or {}),
        relocations=relocations,
    )


def resolve_setup(
    document: dict[str, object],
    expression: data_files.DataFile,
    errors: list[str],
    *,
    trace: data_files.Trace | None = None,
    overrides: dict[Path, bytes] | None = None,
    defaults: dict[str, Path | None] | None = None,
    relocations: dict[Path, Path] | None = None,
) -> dict[str, object]:
    if trace is None:
        trace = data_files.Trace()
    trace.setting_overrides[expression.name] = deepcopy(document)
    root = expression.paths[0].resolve()
    if expression.name not in trace.locations:
        locations: dict[tuple[str | int, ...], data_files.Location] = {}
        data_files.record_locations(document, document, root, [], [], locations, False)
        trace.locations[expression.name] = locations
    if defaults is not None and expression.name in defaults:
        selected = defaults[expression.name]
    else:
        candidate = user_path().absolute()
        if trace is not None and candidate in trace.discovered_defaults:
            selected = trace.discovered_defaults[candidate]
        else:
            selected = (
                candidate.resolve()
                if candidate.exists() or candidate.is_symlink()
                else None
            )
            if trace is not None:
                trace.discovered_defaults[candidate] = selected
    if selected is not None:
        selected = selected.resolve()
    if trace is not None:
        trace.defaults[expression.name] = selected
        trace.roots[expression.name] = root
        trace.relocations_by_setup[expression.name] = dict(relocations or {})
        for original, actual in (relocations or {}).items():
            trace.relocations.setdefault(original, actual)
    inherited: dict[str, object] = {}
    if selected is not None:
        global_trace = data_files.Trace()
        inherited = data_files.load(
            data_files.DataFile(name=expression.name, paths=[selected]),
            True,
            errors,
            trace=global_trace,
            overrides=trace.files | (overrides or {}) if trace else overrides,
            defer_references=True,
            relocations=relocations,
        )
        if 'inheritance' in inherited:
            errors.append(f'{selected}: inheritance is only valid in setup overrides')
        if trace is not None:
            trace.files.update(global_trace.files)
            for p, names in global_trace.consumers.items():
                consumers = trace.consumers.setdefault(p, [])
                consumers.extend(n for n in names if n not in consumers)
            locations = global_trace.locations.get(expression.name, {})
            trace.inherited[expression.name] = locations
            trace.locations[expression.name] = locations | trace.locations.get(
                expression.name, {}
            )
    try:
        result = overlay(inherited, document)
        reads: dict[Path, bytes] = {}
        compiler_inputs.normalize_references(
            result,
            root,
            relocations=relocations,
            origins={
                k: v.file for k, v in trace.locations.get(expression.name, {}).items()
            },
        )
        compiler_inputs.expand_catalogues(
            result,
            root,
            snapshots=reads,
            overrides=trace.files | (overrides or {}) if trace else overrides,
            relocations=relocations,
        )
        if trace is not None:
            trace.files.update(reads)
            for p in reads:
                consumers = trace.consumers.setdefault(p, [])
                if expression.name not in consumers:
                    consumers.append(expression.name)
        return result
    except (OSError, ValueError) as error:
        errors.append(f'{root} ({expression.name}): {error}')
        return document


def overlay(defaults: dict[str, object], setup: dict[str, object]) -> dict[str, object]:
    """Merge table fields, replacing lists and scalars after explicit removals."""
    result = deepcopy(defaults)
    inheritance = setup.get('inheritance', {})
    if not isinstance(inheritance, dict) or inheritance.keys() - {'remove'}:
        raise ValueError('inheritance: expected a table containing only remove')
    removals = inheritance.get('remove', [])
    if not isinstance(removals, list):
        raise ValueError('inheritance.remove: expected a list of table-key paths')
    paths: list[list[str]] = []
    for i, path in enumerate(removals):
        if (
            not isinstance(path, list)
            or not path
            or any(not isinstance(k, str) for k in path)
        ):
            raise ValueError(f'inheritance.remove[{i}]: expected a nonempty key path')
        if any(path[: len(p)] == p or p[: len(path)] == path for p in paths):
            raise ValueError(f'inheritance.remove[{i}]: duplicate or overlapping path')
        paths.append(path)
    for i, path in enumerate(paths):
        table = result
        for key in path[:-1]:
            if not isinstance(child := table.get(key), dict):
                raise ValueError(
                    f'inheritance.remove[{i}]: missing or non-table path {path!r}'
                )
            table = child
        if path[-1] not in table:
            raise ValueError(f'inheritance.remove[{i}]: missing path {path!r}')
        del table[path[-1]]
    merge_values(result, {k: v for k, v in setup.items() if k != 'inheritance'})
    return result


def merge_values(result: dict[str, object], incoming: dict[str, object]) -> None:
    for k, v in incoming.items():
        if (
            k == 'bindings'
            and isinstance(v, dict)
            and isinstance(previous := result.get(k), dict)
        ):
            result[k] = previous | deepcopy(v)
        elif isinstance(v, dict) and isinstance(previous := result.get(k), dict):
            merge_values(previous, v)
        else:
            result[k] = deepcopy(v)
