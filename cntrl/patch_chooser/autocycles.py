"""Select named catalogue programs for automatically populated patch cycles."""

import struct
from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel, ConfigDict, Field, ValidationError

from . import (
    binary_format,
    compiler_errors,
    compiler_inputs,
    data_files,
    global_config,
    models,
    setup_create,
    setup_sync_store,
)
from .compiler_wire import fixed_text

RECIPE = struct.Struct('<HH64s')
CATALOGUE_HEADER = struct.Struct('<4H')
CATALOGUE_PROGRAM = struct.Struct('<H82sIHH')


class Selector(BaseModel, frozen=True):
    instrument: Annotated[str, Field(min_length=1)]
    tag: Annotated[str, Field(min_length=1)] | None = None

    model_config = ConfigDict(extra='forbid')


class AddCycle(BaseModel, frozen=True):
    """Create a patch cycle from a configured instrument catalogue."""

    setup: Annotated[Path, tyro.conf.Positional]


def programs(
    entries: list[models.TablePatch], instrument: models.Instrument
) -> list[models.TablePatch]:
    return sorted(
        (
            e
            for e in entries
            if e.name
            and (e.instrument, e.profile) == (instrument.instrument, instrument.profile)
        ),
        key=lambda e: (-1 if e.bank is None else e.bank, e.program),
    )


def select(
    catalogue: list[models.TablePatch], selector: Selector
) -> list[models.TablePatch]:
    return [e for e in catalogue if selector.tag is None or selector.tag in e.tags]


def recipes(data: bytes, aliases: list[str], count: int) -> dict[int, dict[str, str]]:
    if not data.startswith(binary_format.MAGIC):
        return {}
    result: dict[int, dict[str, str]] = {}
    for record in binary_format.unpack(data, binary_format.Kind.setup).records:
        if record.tag != binary_format.CYCLE_RECIPE:
            continue
        cycle, instrument, tag = RECIPE.unpack(record.data)
        if cycle >= count or instrument >= len(aliases) or cycle in result:
            raise binary_format.DocumentFormatError('invalid automatic cycle recipe')
        value = {'instrument': aliases[instrument]}
        if name := tag.split(b'\0', 1)[0].decode():
            value['tag'] = name
        result[cycle] = value
    return result


def catalogue_program_nodes(data: bytes) -> set[int] | None:
    if not data.startswith(binary_format.MAGIC):
        return None
    for record in binary_format.unpack(data, binary_format.Kind.setup).records:
        if record.tag != binary_format.CYCLE_CATALOGUE:
            continue
        count, tags, _, _ = CATALOGUE_HEADER.unpack_from(record.data)
        cursor = CATALOGUE_HEADER.size + tags * 64
        nodes = set()
        for _ in range(count):
            if cursor + CATALOGUE_PROGRAM.size > len(record.data):
                raise binary_format.DocumentFormatError(
                    'truncated automatic cycle catalogue'
                )
            node, _, _, tags, labels = CATALOGUE_PROGRAM.unpack_from(
                record.data, cursor
            )
            nodes.add(node)
            cursor += CATALOGUE_PROGRAM.size + tags * 2 + labels
        if cursor != len(record.data):
            raise binary_format.DocumentFormatError(
                'invalid automatic cycle catalogue length'
            )
        return nodes
    return None


def pack_catalogue(
    nodes: list[tuple[int, models.TablePatch]],
    records: list[bytes],
    banks: list[int],
    names: list[bytes],
    start: int,
    target: models.Target,
    errors: list[str],
) -> bytes:
    tags = sorted(
        {t for _, e in nodes for t in e.tags}, key=lambda t: (t.casefold(), t)
    )
    if len(nodes) > 65535 or len(tags) > 65535:
        errors.append('automatic cycle catalogue exceeds format capacity')
        return b''
    ids = {t: i for i, t in enumerate(tags)}
    data = bytearray(
        CATALOGUE_HEADER.pack(
            len(nodes), len(tags), target.max_patches, target.max_patch_cycles
        )
    )
    for tag in tags:
        data.extend(
            fixed_text(tag, 64, target, 'catalogue tag', errors, unicode=True).ljust(
                64, b'\0'
            )
        )
    for i, (node, entry) in enumerate(nodes, start):
        labels = b''.join(names[i * 3 : i * 3 + 3])
        tag_ids = sorted({ids[t] for t in entry.tags})
        if node > 65535 or len(labels) > 65535:
            errors.append('automatic cycle programme exceeds format capacity')
            break
        data.extend(
            CATALOGUE_PROGRAM.pack(
                node, records[i], banks[i], len(tag_ids), len(labels)
            )
        )
        data.extend(struct.pack(f'<{len(tag_ids)}H', *tag_ids))
        data.extend(labels)
    return bytes(data)


def run(command: AddCycle) -> None:
    path = command.setup.expanduser().resolve()
    initial = setup_sync_store.read_sync_file(path)
    expression = data_files.DataFile(name=path.stem, paths=[path])
    errors: list[str] = []
    document = global_config.load_setup(expression, True, errors)
    if errors:
        raise compiler_errors.CompilationError(errors)
    try:
        setup = models.Setup.model_validate(document)
        entries = compiler_inputs.join_tables(
            [
                {'patches': [p.model_dump() for p in setup.definitions.programs]}
                if setup.definitions
                else {'patches': []},
                {'instruments': setup.instruments},
            ],
            errors,
        )
        catalogues = {
            n: c
            for n, r in setup.instruments.items()
            if (c := programs(entries, compiler_inputs.instrument_from_source(n, r)))
        }
    except ValidationError as error:
        raise compiler_errors.CompilationError(
            compiler_errors.validation_errors('setup', error)
        ) from error
    if errors:
        raise compiler_errors.CompilationError(errors)
    if not catalogues:
        raise compiler_errors.CompilationError(['No instruments with named programs'])

    aliases = list(catalogues)
    alias = aliases[choose('Instrument', aliases)]
    catalogue = catalogues[alias]
    tags = sorted({t for e in catalogue for t in e.tags}, key=str.casefold)
    tag = [None, *tags][choose('Programs', ['All programs', *tags])] if tags else None
    selector = Selector(instrument=alias, tag=tag)
    base = f'{alias} {tag}' if tag else alias
    used = {n.casefold() for n in setup.patch_cycles}
    default = base.encode()[:63].decode(errors='ignore')
    suffix = 2
    while default.casefold() in used:
        tail = f' {suffix}'
        default = base.encode()[: 63 - len(tail)].decode(errors='ignore') + tail
        suffix += 1
    count = len(select(catalogue, selector))
    while True:
        name = setup_create.ask(f'Cycle name ({count} programs)', default)
        if (
            name.casefold() not in used
            and len(name.encode()) < 64
            and name.isprintable()
        ):
            break
        print('Choose an unused printable name of at most 63 UTF-8 bytes.')
    if setup_create.ask(
        f'Add {name} with {count} programs? (Y/n)', 'y'
    ).lower() not in ('y', 'yes'):
        return
    authored = setup_sync_store.parse_record(path, initial)
    cycles = authored.setdefault('patch_cycles', tomlkit.table())
    cycle = tomlkit.inline_table()
    cycle.update(selector.model_dump(exclude_none=True))
    cycles[name] = cycle
    with setup_sync_store.sync_files():
        if path.read_bytes() != initial:
            raise compiler_errors.CompilationError(
                [f'{path}: setup changed during editing']
            )
        setup_sync_store.replace(path, tomlkit.dumps(authored).encode())
    print(f'Added {name} to {path}.')


def choose(prompt: str, choices: list[str]) -> int:
    for i, choice in enumerate(choices, 1):
        print(f'{i}. {choice}')
    while True:
        number = setup_create.ask(prompt, '1')
        if number.isascii() and number.isdecimal() and 1 <= int(number) <= len(choices):
            return int(number) - 1
        print('Choose a number from the list.')
