"""Ordered data-file expressions, logical names and accumulated merge errors."""

from copy import deepcopy
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, ConfigDict, Field
from reccy.configuration import units

from .compiler import CompilationError, read_document


class DataFile(BaseModel, frozen=True):
    name: str
    paths: list[Path]


class Sources(BaseModel, frozen=True):
    document: Annotated[list[str], tyro.conf.Positional]
    target: list[str]
    definitions: list[str] = Field(default_factory=list)
    table: Annotated[list[list[str]], tyro.conf.UseAppendAction] = Field(
        default_factory=list
    )
    avoid_confict: bool = True
    memory_margin: units.Bytes = Field(default=16192, ge=0)

    model_config = ConfigDict(extra='forbid')


class Location(BaseModel, frozen=True):
    file: Path
    path: list[str | int]


class Trace(BaseModel, frozen=True):
    """Source bytes and effective authoring paths mapped to original file paths."""

    files: dict[Path, bytes] = Field(default_factory=dict)
    locations: dict[str, dict[tuple[str | int, ...], Location]] = Field(
        default_factory=dict
    )
    consumers: dict[Path, list[str]] = Field(default_factory=dict)


def validate_name(name: str) -> str:
    if (
        not 1 <= len(name) <= 63
        or name in ('.', '..')
        or name.endswith((' ', '.'))
        or name.split('.', 1)[0].rstrip(' ').upper() in WINDOWS_DEVICES
        or any(ord(c) < 32 or ord(c) > 126 or c in '<>:"/\\|?*' for c in name)
    ):
        raise ValueError(
            f'{name!r}: invalid setup name (1..63 filename-safe ASCII characters)'
        )
    return name


def expressions(
    arguments: list[str], errors: list[str] | None = None
) -> list[DataFile]:
    groups: list[list[str]] = []
    own_errors = errors is None
    if errors is None:
        errors = []
    joining = False
    for a in arguments:
        if a == '+':
            if not groups or joining:
                errors.append('unexpected +: expected a data file')
            joining = True
        else:
            if joining and groups:
                groups[-1].append(a)
            else:
                groups.append([a])
            joining = False
    if joining:
        errors.append('trailing +: expected a data file')
    result: list[DataFile] = []
    for g in groups:
        name: str | None = None
        paths: list[Path] = []
        for i, a in enumerate(g):
            prefix = ''
            j = 0
            while j < len(a):
                if a[j : j + 2] in ('==', '\\='):
                    prefix += '='
                    j += 2
                elif a[j] == '=':
                    break
                else:
                    prefix += a[j]
                    j += 1
            if j < len(a):
                if i:
                    errors.append(
                        f'{a}: a name is allowed only before the first operand'
                    )
                else:
                    name = prefix
                a = a[j + 1 :]
            if not a:
                errors.append('empty data-file path')
            paths.append(Path(a))
        result.append(
            DataFile(
                name=name if name is not None else '+'.join(p.stem for p in paths),
                paths=paths,
            )
        )
    if errors and own_errors:
        raise CompilationError(errors)
    return result


def load(
    expression: DataFile,
    avoid_confict: bool,
    errors: list[str],
    *,
    trace: Trace | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> dict[str, object]:
    result: dict[str, object] = {}
    origins: dict[str, str] = {}
    for p in expression.paths:
        try:
            reads: dict[Path, bytes] = {}
            value = read_document(p, snapshots=reads, overrides=overrides)
        except (OSError, ValueError) as error:
            errors.append(f'{p}: {error}')
            continue
        if not isinstance(value, dict):
            errors.append(f'{p}: expected a dictionary')
            continue
        _merge(result, value, origins, str(p), '', avoid_confict, errors)
        if trace is not None:
            trace.files.update(reads)
            for f in reads:
                names = trace.consumers.setdefault(f, [])
                if expression.name not in names:
                    names.append(expression.name)
            locations = trace.locations.setdefault(expression.name, {})
            record_locations(
                value, result, p.resolve(), [], [], locations, avoid_confict
            )
    return result


def record_locations(
    value: object,
    merged: object,
    file: Path,
    path: list[str | int],
    source: list[str | int],
    locations: dict[tuple[str | int, ...], Location],
    append: bool,
) -> None:
    locations.setdefault(tuple(path), Location(file=file, path=source))
    if isinstance(value, dict) and isinstance(merged, dict):
        for k, v in value.items():
            if k in merged:
                record_locations(
                    v, merged[k], file, [*path, k], [*source, k], locations, append
                )
    elif isinstance(value, list) and isinstance(merged, list):
        start = len(merged) - len(value) if append else 0
        locations[tuple(path)] = Location(file=file, path=source)
        for i, v in enumerate(value):
            record_locations(
                v, v, file, [*path, i + start], [*source, i], locations, append
            )
    else:
        locations[tuple(path)] = Location(file=file, path=source)


def load_target(
    expression: DataFile,
    avoid_confict: bool,
    errors: list[str],
    *,
    trace: Trace | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> dict[str, object]:
    """Resolve resource references at their source file before combining targets."""
    result: dict[str, object] = {}
    origins: dict[str, str] = {}
    for p in expression.paths:
        incoming = load(
            DataFile(name=expression.name, paths=[p]),
            avoid_confict,
            errors,
            trace=trace,
            overrides=overrides,
        )
        if isinstance(resources := incoming.get('font_sources'), dict):
            incoming['font_sources'] = {
                n: str((p.resolve().parent / v).resolve()) if isinstance(v, str) else v
                for n, v in resources.items()
            }
        _merge(result, incoming, origins, str(p), '', avoid_confict, errors)
    return result


def _merge(
    result: dict[str, object],
    incoming: dict[str, object],
    origins: dict[str, str],
    source: str,
    prefix: str,
    avoid_confict: bool,
    errors: list[str],
) -> None:
    for key, new in incoming.items():
        path = f'{prefix}.{key}' if prefix else key
        if key not in result:
            if isinstance(new, dict):
                child: dict[str, object] = {}
                result[key] = child
                _merge(child, new, origins, source, path, avoid_confict, errors)
            else:
                result[key] = deepcopy(new)
            origins[path] = source
            continue
        old = result[key]
        positional_programs = path.startswith('instruments.') and (
            key == 'programs' or prefix.endswith('.programs')
        )
        if isinstance(old, dict) and isinstance(new, dict):
            _merge(old, new, origins, source, path, avoid_confict, errors)
        elif type(old) is not type(new):
            errors.append(
                f'{origins[path]} and {source}: {path}: incompatible value types'
            )
        elif positional_programs and isinstance(old, list) and isinstance(new, list):
            if old != new:
                errors.append(
                    f'{origins[path]} and {source}: {path}: '
                    'conflicting program positions'
                )
        elif isinstance(old, list) and isinstance(new, list) and avoid_confict:
            old.extend(deepcopy(new))
            origins[path] = source
        elif avoid_confict or old == new:
            result[key] = deepcopy(new)
            origins[path] = source
        else:
            errors.append(
                f'{origins[path]} and {source}: {path}: '
                f'conflicting values {old!r} and {new!r}'
            )


WINDOWS_DEVICES = {
    'CON',
    'PRN',
    'AUX',
    'NUL',
    *(f'{p}{i}' for p in ('COM', 'LPT') for i in range(1, 10)),
}
