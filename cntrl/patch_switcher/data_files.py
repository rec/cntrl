"""Ordered data-file expressions, logical names and accumulated merge errors."""

from copy import deepcopy
from pathlib import Path

from pydantic import BaseModel

from .compiler import CompilationError, read_document


class DataFile(BaseModel, frozen=True):
    name: str
    paths: list[Path]


def validate_name(name: str) -> str:
    if (
        not 1 <= len(name) <= 63
        or name in ('.', '..')
        or name.endswith((' ', '.'))
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
    expression: DataFile, avoid_confict: bool, errors: list[str]
) -> dict[str, object]:
    result: dict[str, object] = {}
    origins: dict[str, str] = {}
    for p in expression.paths:
        try:
            value = read_document(p)
        except (OSError, ValueError) as error:
            errors.append(f'{p}: {error}')
            continue
        if not isinstance(value, dict):
            errors.append(f'{p}: expected a dictionary')
            continue
        _merge(result, value, origins, str(p), '', avoid_confict, errors)
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
        if isinstance(old, dict) and isinstance(new, dict):
            _merge(old, new, origins, source, path, avoid_confict, errors)
        elif type(old) is not type(new):
            errors.append(
                f'{origins[path]} and {source}: {path}: incompatible value types'
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
