"""Read setup sources and merge instrument catalogues for compilation."""

import json
import ntpath
import posixpath
from hashlib import sha256
from pathlib import Path
from typing import cast

import tomlkit
from pydantic import ValidationError

from .. import source_files
from . import models
from .compiler_errors import validation_errors


class DocumentSourceError(ValueError):
    """A source document has invalid catalogue references or a wrong format."""


def read_document(
    path: Path,
    *,
    _ancestors: tuple[Path, ...] = (),
    snapshots: dict[Path, bytes] | None = None,
    overrides: dict[Path, bytes] | None = None,
    defer_references: bool = False,
    relocations: dict[Path, Path] | None = None,
) -> object:
    resolved = path.resolve()
    if relocations is not None:
        resolved = relocations.get(path, relocations.get(resolved, resolved))
    origin = next(
        (k for k, v in (relocations or {}).items() if v.resolve() == resolved),
        path,
    )
    if resolved in _ancestors:
        raise DocumentSourceError(f'{path}: catalogue reference cycle')
    content = (
        overrides[resolved]
        if overrides is not None and resolved in overrides
        else source_files.read(resolved, snapshots)
    )
    if snapshots is not None:
        snapshots[resolved] = content
    source = content.decode()
    if resolved.suffix == '.json':
        document = json.loads(source)
    elif resolved.suffix == '.toml':
        document = tomlkit.parse(source).unwrap()
    else:
        raise DocumentSourceError(f'{path}: expected .toml or .json')
    if isinstance(document, dict) and isinstance(
        patches := document.get('patches'), list
    ):
        for p in patches:
            if isinstance(p, dict):
                p.setdefault('instrument', Path(ntpath.basename(str(origin))).stem)
    if not defer_references and isinstance(document, dict):
        normalize_references(document, path, relocations=relocations)
        expand_catalogues(
            document,
            path,
            snapshots=snapshots,
            overrides=overrides,
            ancestors=_ancestors + (resolved,),
            relocations=relocations,
        )
    return document


def normalize_references(
    document: dict[str, object],
    path: Path,
    *,
    relocations: dict[Path, Path] | None = None,
    origins: dict[tuple[str | int, ...], Path] | None = None,
) -> None:
    def reference(value: str, context: tuple[str | int, ...]) -> str:
        while origins is not None and context not in origins and context:
            context = context[:-1]
        source = (origins or {}).get(context, path)
        origin = next(
            (
                k
                for k, v in (relocations or {}).items()
                if v.resolve() == source.resolve()
            ),
            source,
        )
        return str(source_reference(origin, value, relocations))

    def artwork(node: object, context: tuple[str | int, ...]) -> None:
        if isinstance(node, dict):
            fields = cast(dict[str, object], node)
            if (
                isinstance(image := fields.get('image'), str)
                and image
                and (
                    context[-1:] in (('style',), ('status_style',), ('contact_style',))
                    or context[-2:-1] in (('line_styles',), ('tag_styles',))
                )
            ):
                fields['image'] = reference(image, (*context, 'image'))
            for name, value in fields.items():
                artwork(value, (*context, name))
        elif isinstance(node, list):
            for i, value in enumerate(node):
                artwork(value, (*context, i))

    artwork(document, ())
    for field in ('fonts', 'font_sources'):
        if isinstance(fonts := document.get(field), dict):
            for n, f in fonts.items():
                if isinstance(f, str):
                    fonts[n] = reference(f, (field, n))
    for field in ('splash_movie', 'idle_movie'):
        if field in document:
            movies = models.parse_movies(document[field])
            if isinstance(movies, list) and all(
                isinstance(m, str) and m for m in movies
            ):
                document[field] = [
                    reference(m, (field, i)) for i, m in enumerate(movies)
                ]
    if isinstance(instruments := document.get('instruments'), dict):
        for n, instrument in instruments.items():
            if not isinstance(instrument, dict):
                continue
            references = instrument.get('catalogues')
            if isinstance(references, list) and all(
                isinstance(r, str) for r in references
            ):
                instrument['catalogues'] = [
                    reference(r, ('instruments', n, 'catalogues', i))
                    for i, r in enumerate(references)
                ]


def expand_catalogues(
    document: dict[str, object],
    path: Path,
    *,
    snapshots: dict[Path, bytes] | None = None,
    overrides: dict[Path, bytes] | None = None,
    ancestors: tuple[Path, ...] = (),
    relocations: dict[Path, Path] | None = None,
) -> None:
    if isinstance(instruments := document.get('instruments'), dict):
        for alias, instrument in instruments.items():
            if not isinstance(instrument, dict) or 'catalogues' not in instrument:
                continue
            if 'programs' in instrument:
                raise DocumentSourceError(
                    f'instruments.{alias}: specify catalogues or programs, not both'
                )
            instrument['programs'] = _shared_programs(
                instrument.pop('catalogues'),
                path,
                ancestors or (path.resolve(),),
                snapshots,
                overrides,
                relocations,
            )


def source_reference(
    origin: Path, reference: str, relocations: dict[Path, Path] | None = None
) -> Path:
    """Resolve source-relative paths, including recovered foreign-platform rigs."""
    if not relocations or origin not in relocations:
        return (origin.parent / reference).resolve()
    if Path(reference) in relocations.values():
        return Path(reference)
    original = str(origin)
    if ntpath.splitdrive(original)[0] or '\\' in original:
        key = Path(ntpath.normpath(ntpath.join(ntpath.dirname(original), reference)))
        folded = ntpath.normcase(str(key))
        for source, target in relocations.items():
            if ntpath.normcase(str(source)) == folded:
                return target
    else:
        key = Path(
            posixpath.normpath(posixpath.join(posixpath.dirname(original), reference))
        )
    if key not in relocations:
        actual = relocations[origin]
        return (
            actual.parent
            / '.cntrl-unavailable-source'
            / sha256(str(key).encode()).hexdigest()
            / (ntpath.basename(reference) or 'source')
        )
    return relocations[key]


def join_tables(tables: list[object], errors: list[str]) -> list[models.TablePatch]:
    entries: dict[tuple[str, str, int, int | None], models.TablePatch] = {}
    for index, table in enumerate(tables):
        if not isinstance(table, dict) or not (
            isinstance(table.get('patches'), list)
            or isinstance(table.get('instruments'), dict)
        ):
            errors.append(f'table[{index + 1}]: expected a patches list')
            continue
        rows = table.get('patches', [])
        rows = rows if isinstance(rows, list) else []
        rows = rows + _catalogue_rows(table, errors)
        for row, raw in enumerate(rows):
            context = f'table[{index + 1}].patches[{row + 1}]'
            try:
                entry = models.TablePatch.model_validate(raw)
            except ValidationError as error:
                errors.extend(validation_errors(context, error))
                continue
            key = (entry.instrument, entry.profile, entry.program, entry.bank)
            if (previous := entries.get(key)) is None:
                entries[key] = entry
                continue
            merged = previous.model_dump()
            merged['tags'] = list(dict.fromkeys(previous.tags + entry.tags))
            for field in (
                'name',
                'display_name',
                'names',
                'range',
                'description',
                'style',
            ):
                old, new = getattr(previous, field), getattr(entry, field)
                if old is None or old == {}:
                    merged[field] = new
                elif new is not None and new != {} and old != new:
                    errors.append(f'{context}: conflicting {field} for {key}')
            entries[key] = models.TablePatch.model_validate(merged)
    for table in tables:
        if not isinstance(table, dict):
            continue
        instruments = table.get('instruments', {})
        if not isinstance(instruments, dict):
            continue
        for alias, instrument in instruments.items():
            if not isinstance(instrument, dict) or 'tags' not in instrument:
                continue
            context = f'instruments.{alias}.tags'
            tags = instrument['tags']
            if not isinstance(tags, dict):
                errors.append(
                    f'{context}: expected tag names mapped to program-name lists'
                )
                continue
            candidates = [
                k
                for k in entries
                if k[:2]
                == (
                    instrument.get('instrument', alias),
                    instrument.get('profile', 'default'),
                )
            ]
            shared = [
                k
                for k, v in entries.items()
                if instrument.get('instrument', alias) in v.names
            ]
            for tag, names in tags.items():
                if (
                    not isinstance(tag, str)
                    or not isinstance(names, list)
                    or any(not isinstance(n, str) for n in names)
                ):
                    errors.append(f'{context}.{tag}: expected a list of program names')
                    continue
                # A shared tags file can classify catalogues not loaded in this show.
                if not candidates and not shared:
                    continue
                for name in names:
                    matches = list(
                        dict.fromkeys(
                            [k for k in candidates if entries[k].name == name]
                            + [
                                k
                                for k in shared
                                if entries[k].names[instrument.get('instrument', alias)]
                                == name
                            ]
                        )
                    )
                    if not matches and candidates:
                        errors.append(f'{context}.{tag}: unknown program name {name!r}')
                    for key in matches:
                        entry = entries[key]
                        entries[key] = entry.model_copy(
                            update={'tags': list(dict.fromkeys(entry.tags + [tag]))}
                        )
    return list(entries.values())


def instrument_from_source(alias: str, raw: object) -> models.Instrument:
    if isinstance(raw, dict):
        raw = {k: v for k, v in raw.items() if k not in ('programs', 'tags')}
        raw.setdefault('instrument', alias)
    return models.Instrument.model_validate(raw)


def _catalogue_rows(table: dict[str, object], errors: list[str]) -> list[object]:
    rows: list[object] = []
    instruments = table.get('instruments', {})
    if not isinstance(instruments, dict):
        return rows
    for alias, instrument in instruments.items():
        if not isinstance(instrument, dict) or 'programs' not in instrument:
            continue
        programs = instrument['programs']
        if isinstance(programs, list):
            banks = {None: programs}
        elif isinstance(programs, dict):
            banks = programs
        else:
            errors.append(
                f'instruments.{alias}.programs: expected an array or bank table'
            )
            continue
        for bank, values in banks.items():
            context = f'instruments.{alias}.programs'
            if bank is not None:
                context += f'.{bank}'
                if (
                    not isinstance(bank, str)
                    or not bank.isascii()
                    or not bank.isdecimal()
                ):
                    errors.append(f'{context}: bank key must be a nonnegative integer')
                    continue
            if not isinstance(values, list) or len(values) > 128:
                errors.append(f'{context}: expected an array of at most 128 programs')
                continue
            for number, value in enumerate(values, 1):
                if value == {}:
                    continue
                if isinstance(value, str):
                    value = {'name': value}
                if not isinstance(value, dict):
                    errors.append(
                        f'{context}[{number}]: expected a name or metadata table'
                    )
                    continue
                address = {
                    'instrument': instrument.get('instrument', alias),
                    'profile': instrument.get('profile', 'default'),
                    'program': number,
                    'bank': int(bank) if bank is not None else None,
                }
                if any(k in value for k in address):
                    errors.append(
                        f'{context}[{number}]: program address comes from its position'
                    )
                    continue
                rows.append(value | address)
    return rows


def _shared_programs(
    references: object,
    source: Path,
    ancestors: tuple[Path, ...],
    snapshots: dict[Path, bytes] | None = None,
    overrides: dict[Path, bytes] | None = None,
    relocations: dict[Path, Path] | None = None,
) -> list[object]:
    if (
        not isinstance(references, list)
        or not references
        or any(not isinstance(p, str) for p in references)
    ):
        raise DocumentSourceError(
            f'{source}: catalogues must be a nonempty list of file paths'
        )
    catalogues: dict[str, list[object]] = {}
    for reference in references:
        path = source.parent / reference
        document = read_document(
            path,
            _ancestors=ancestors,
            snapshots=snapshots,
            overrides=overrides,
            relocations=relocations,
        )
        if (
            not isinstance(document, dict)
            or not isinstance(instruments := document.get('instruments'), dict)
            or len(instruments) != 1
        ):
            raise DocumentSourceError(
                f'{path}: a shared catalogue must describe exactly one instrument'
            )
        alias, instrument = next(iter(instruments.items()))
        if not isinstance(instrument, dict):
            raise DocumentSourceError(f'{path}: invalid instrument catalogue')
        identity = instrument.get('instrument', alias)
        if not isinstance(identity, str) or identity in catalogues:
            raise DocumentSourceError(
                f'{path}: catalogue instrument names must be distinct'
            )
        programs = instrument.get('programs')
        if isinstance(programs, dict) and len(programs) == 1:
            programs = next(iter(programs.values()))
        if not isinstance(programs, list) or not 1 <= len(programs) <= 128:
            raise DocumentSourceError(
                f'{path}: select a single nonempty bank of at most 128 programs'
            )
        catalogues[identity] = programs
    counts = {len(v) for v in catalogues.values()}
    if len(counts) != 1:
        raise DocumentSourceError(
            f'{source}: shared catalogues must have the same program count'
        )
    combined: list[object] = []
    for number in range(next(iter(counts))):
        names: dict[str, str] = {}
        for identity, programs in catalogues.items():
            entry = programs[number]
            name = entry.get('name') if isinstance(entry, dict) else entry
            if not isinstance(name, str) or not name:
                raise DocumentSourceError(
                    f'{source}: {identity} program {number + 1} needs a name'
                )
            names[identity] = name
        combined.append(
            {'name': ' / '.join(dict.fromkeys(names.values())), 'names': names}
        )
    return combined
