"""Compile a complete show, accumulating errors before producing any binary."""

import json
import re
import struct
from pathlib import Path
from zlib import crc32

import tomlkit
from pydantic import ValidationError

from . import display, models


class CompilationError(ValueError):
    def __init__(self, errors: list[str]) -> None:
        self.errors = errors
        super().__init__(
            'Configuration errors:\n' + '\n'.join(f'- {e}' for e in errors)
        )


def read_document(path: Path) -> object:
    with path.open() as source:
        if path.suffix == '.json':
            document = json.load(source)
        elif path.suffix == '.toml':
            document = tomlkit.load(source).unwrap()
        else:
            raise ValueError(f'{path}: expected .toml or .json')
    if isinstance(document, dict) and isinstance(
        patches := document.get('patches'), list
    ):
        for p in patches:
            if isinstance(p, dict):
                p.setdefault('instrument', path.stem)
    if isinstance(document, dict) and isinstance(fonts := document.get('fonts'), dict):
        for n, f in fonts.items():
            if isinstance(f, str):
                fonts[n] = str((path.parent / f).resolve())
    return document


def normalize_patch(value: object) -> models.Patch:
    if not isinstance(value, dict):
        raise ValueError('patch must be a table/object')
    fields = dict(value)
    if 'patch' in fields:
        shorthand = fields.pop('patch')
        if not isinstance(shorthand, str) or len(shorthand) < 2:
            raise ValueError('patch shorthand needs a delimiter and fields')
        parts = shorthand[1:].split(shorthand[0])
        if not 2 <= len(parts) <= 5 or not parts[0] or not parts[1]:
            raise ValueError('expected instrument, program[, volume, pan, bank]')
        for name, part in zip(
            ('instrument', 'program', 'volume', 'pan', 'bank'), parts, strict=False
        ):
            if not part:
                continue
            if name in fields:
                raise ValueError(f'{name} supplied both separately and in shorthand')
            if name in ('volume', 'pan', 'bank'):
                fields[name] = int(part)
            elif name == 'program' and re.fullmatch('[0-9]+', part):
                fields[name] = int(part)
            else:
                fields[name] = part
    return models.Patch.model_validate(fields)


def compile_show(
    document: object,
    definitions: object,
    tables: list[object],
    target: models.Target,
    assets: dict[int, bytes] | None = None,
) -> bytes:
    errors: list[str] = []
    generated: dict[int, bytes] = {}
    try:
        show = models.Show.model_validate(document)
    except ValidationError as error:
        raise CompilationError(_validation_errors('show', error)) from error
    if show.fonts:
        from ..maker.morningstar.mc.font_assets import prepare_fonts

        try:
            show, target, generated = prepare_fonts(show, target)
        except (OSError, ValueError) as error:
            raise CompilationError([f'fonts: {error}']) from error
        if assets is not None:
            assets.update(generated)
    setups: dict[tuple[str, str], models.Setup] = {}
    embedded_programs: list[dict[str, object]] = []
    try:
        parsed = show.definitions or models.Definitions.model_validate(definitions)
        embedded_programs = [p.model_dump() for p in parsed.programs]
        for s in parsed.setups:
            key = (s.instrument, s.name)
            if key in setups:
                errors.append(f'definitions: duplicate setup {key}')
            setups[key] = s
            if any(b < 0 for b in s.banks):
                errors.append(f'definitions: negative bank in {key}')
    except ValidationError as error:
        errors.extend(_validation_errors('definitions', error))
    entries = _join_tables([{'patches': embedded_programs}, *tables], errors)
    instruments: dict[str, models.Instrument] = {}
    instrument_records: list[bytes] = []
    for alias, raw in show.instruments.items():
        try:
            instrument = models.Instrument.model_validate(raw)
        except ValidationError as error:
            errors.extend(_validation_errors(f'instruments.{alias}', error))
            continue
        if (instrument.instrument, instrument.setup) not in setups:
            errors.append(f'instruments.{alias}: unknown instrument/setup')
        if instrument.output not in target.outputs:
            errors.append(
                f'instruments.{alias}: unavailable output {instrument.output!r}'
            )
        instruments[alias] = instrument
        instrument_records.append(
            struct.pack(
                '<32s32sB3x',
                _text(alias, 32, target, f'instruments.{alias}', errors),
                _text(
                    instrument.output, 32, target, f'instruments.{alias}.output', errors
                ),
                instrument.channel - 1,
            )
        )
    if not instruments:
        errors.append('instruments: at least one instrument is required')
    if not show.patches:
        errors.append('patches: at least one category is required')
    categories: list[bytes] = []
    patches: list[bytes] = []
    logical_banks: list[int] = []
    name_records: list[bytes] = []
    program_names: dict[tuple[str, str, int, int | None], list[str | None]] = {}
    display_patches: list[display.DisplayPatch] = []
    for category, values in show.patches.items():
        category_text = _text(category, 64, target, f'patches.{category}', errors)
        if not isinstance(values, list):
            errors.append(f'patches.{category}: expected a list of patches')
            continue
        if not values:
            errors.append(f'patches.{category}: category is empty')
        start = len(patches)
        for index, value in enumerate(values):
            context = f'patches.{category}[{index + 1}]'
            try:
                patch = normalize_patch(value)
            except ValidationError as error:
                errors.extend(_validation_errors(context, error))
                continue
            except ValueError as error:
                errors.append(f'{context}: {error}')
                continue
            if patch.instrument not in instruments:
                errors.append(
                    f'{context}: unknown or invalid instrument {patch.instrument!r}'
                )
                continue
            instrument = instruments[patch.instrument]
            setup = setups.get((instrument.instrument, instrument.setup))
            if setup is None:
                errors.append(f'{context}: unknown or invalid setup')
                continue
            bank = patch.bank
            program = patch.program
            name = patch.name
            matched = None
            if isinstance(program, str):
                try:
                    pattern = re.compile(program)
                except re.error as error:
                    errors.append(f'{context}: invalid regex: {error}')
                    continue
                matches = [
                    e
                    for e in entries
                    if e.instrument == instrument.instrument
                    and e.setup == instrument.setup
                    and (bank is None or e.bank == bank)
                    and e.name is not None
                    and pattern.search(e.name)
                ]
                if not matches:
                    errors.append(f'{context}: no patch matches {program!r}')
                    continue
                if len(matches) > 1 and not show.take_first_match:
                    errors.append(
                        f'{context}: multiple matches for {program!r}: '
                        + ', '.join(
                            f'{e.name} (PC {e.program}, bank {e.bank})' for e in matches
                        )
                    )
                    continue
                matched = matches[0]
                program, bank = matched.program, matched.bank
            else:
                matched = next(
                    (
                        e
                        for e in entries
                        if (e.instrument, e.setup, e.program, e.bank)
                        == (instrument.instrument, instrument.setup, program, bank)
                    ),
                    None,
                )
            program_display_name = matched.display_name if matched else None
            if matched is not None and matched.name is not None:
                if name is not None and name != matched.name:
                    errors.append(
                        f'{context}: name differs from synthesizer name '
                        f'{matched.name!r}; use display_name to override it'
                    )
                name = matched.name
            identity = (instrument.instrument, instrument.setup, program, bank)
            names = [name, program_display_name]
            if identity in program_names and program_names[identity] != names:
                errors.append(f'{context}: conflicting program names for {identity}')
            program_names[identity] = names
            mapping = setup.banks.get(bank) if bank is not None else None
            if bank is not None and mapping is None:
                errors.append(f'{context}: bank {bank} has no mapping in this setup')
                continue
            display_name = patch.display_name
            if display_name is None:
                display_name = program_display_name
            if display_name is None:
                display_name = name
            if display_name is None:
                display_name = (
                    f'PC {program}' if bank is None else f'Bank {bank} / PC {program}'
                )
            patches.append(
                struct.pack(
                    '<HHBBBBB3x64s',
                    list(instruments).index(patch.instrument),
                    len(categories),
                    program - 1,
                    _optional(mapping.msb if mapping else None),
                    _optional(mapping.lsb if mapping else None),
                    _optional(patch.volume),
                    _optional(patch.pan + 64 if patch.pan is not None else None),
                    _text(display_name, 64, target, context + '.display_name', errors),
                )
            )
            display_patches.append(
                display.DisplayPatch(
                    values={
                        'group.name': category,
                        'display_name': display_name,
                        'name': name,
                        'program': program,
                        'bank': bank,
                        'volume': patch.volume,
                        'pan': patch.pan,
                        'instrument': patch.instrument,
                    },
                    style=patch.style,
                    program_style=matched.style if matched else None,
                    categories=patch.categories,
                    program_categories=matched.categories if matched else [],
                )
            )
            logical_banks.append(0xFFFFFFFF if bank is None else bank)
            for field, text in (
                ('name', name),
                ('program.display_name', program_display_name),
                ('display_name', patch.display_name),
            ):
                if text is None:
                    name_records.append(struct.pack('<H', 65535))
                else:
                    encoded = text.encode('utf-8')
                    if len(encoded) >= 65535:
                        errors.append(
                            f'{context}.{field}: name exceeds storage capacity'
                        )
                    name_records.append(
                        struct.pack('<H', min(len(encoded), 65534)) + encoded[:65534]
                    )
            if bank is not None and bank >= 0xFFFFFFFF:
                errors.append(f'{context}: bank exceeds storage capacity')
        count = len(patches) - start
        # Report over-capacity documents rather than overflowing packed fields.
        categories.append(
            struct.pack('<64sHH', category_text, min(start, 65535), min(count, 65535))
        )
    for label, count, maximum in (
        ('instruments', len(instruments), target.max_instruments),
        ('categories', len(categories), target.max_categories),
        ('patches', len(patches), target.max_patches),
    ):
        if count > maximum:
            errors.append(f'{label}: {count} exceeds target capacity {maximum}')
    screen_records: list[bytes] = []
    references: list[list[int]] = []
    display_records = b''
    try:
        screen_records, references, display_records = display.compile_display(
            show, display_patches, target
        )
        if generated:
            from ..maker.morningstar.mc.font_assets import validate_budget

            validate_budget(screen_records, display_records, generated)
    except ValueError as error:
        errors.append(str(error))
    if len(references) == len(patches):
        patches = [
            p + struct.pack('<3H', *references[i]) for i, p in enumerate(patches)
        ]
    # Immutable identity/mapping metadata enables resolved, editable exports.
    # Runtime bytes remain the single authoritative copy of editable values.
    mappings: list[bytes] = []
    identities: list[bytes] = []
    for index, (alias, instrument) in enumerate(instruments.items()):
        identities.append(
            struct.pack(
                '<32s32s',
                _text(
                    instrument.instrument,
                    32,
                    target,
                    f'instruments.{alias}.instrument',
                    errors,
                ),
                _text(
                    instrument.setup, 32, target, f'instruments.{alias}.setup', errors
                ),
            )
        )
        if setup := setups.get((instrument.instrument, instrument.setup)):
            for bank, mapping in setup.banks.items():
                if not 0 <= bank < 0xFFFFFFFF:
                    errors.append(f'instruments.{alias}: bank exceeds storage capacity')
                    continue
                mappings.append(
                    struct.pack(
                        '<HIBB',
                        index,
                        bank,
                        _optional(mapping.msb),
                        _optional(mapping.lsb),
                    )
                )
    if len(mappings) > 65535:
        errors.append('too many bank mappings')
    metadata = METADATA.pack(
        b'CMTA',
        min(len(mappings), 65535),
        0 if show.transition_ms is None else show.transition_ms,
    )
    body = b''.join(screen_records + instrument_records + categories + patches)
    body += metadata + b''.join(identities + mappings)
    body += b''.join(struct.pack('<I', min(b, 0xFFFFFFFF)) for b in logical_banks)
    body += b'CNAM' + b''.join(name_records)
    body += display_records
    if len(body) + HEADER.size > target.max_document_bytes:
        errors.append(
            f'document: {len(body) + HEADER.size} bytes exceeds target capacity '
            f'{target.max_document_bytes}'
        )
    if errors:
        raise CompilationError(errors)
    return (
        HEADER.pack(
            b'CSW1',
            DOCUMENT_VERSION,
            show.chord_ms,
            HEADER.size + len(body),
            len(instruments),
            len(categories),
            len(patches),
            show.brightness
            | (int(show.splash) << 8)
            | (int(show.usb_midi_interface) << 9),
            crc32(body),
            show.screensaver_timeout * 1000,
        )
        + body
    )


def _validation_errors(context: str, error: ValidationError) -> list[str]:
    return [
        f'{context}.{".".join(str(p) for p in e["loc"])}: {e["msg"]}'
        for e in error.errors()
    ]


def _join_tables(tables: list[object], errors: list[str]) -> list[models.TablePatch]:
    entries: dict[tuple[str, str, int, int | None], models.TablePatch] = {}
    for index, table in enumerate(tables):
        if not isinstance(table, dict) or not isinstance(table.get('patches'), list):
            errors.append(f'table[{index + 1}]: expected a patches list')
            continue
        for row, raw in enumerate(table['patches']):
            context = f'table[{index + 1}].patches[{row + 1}]'
            try:
                entry = models.TablePatch.model_validate(raw)
            except ValidationError as error:
                errors.extend(_validation_errors(context, error))
                continue
            key = (entry.instrument, entry.setup, entry.program, entry.bank)
            if (previous := entries.get(key)) is None:
                entries[key] = entry
                continue
            merged = previous.model_dump()
            merged['tags'] = list(dict.fromkeys(previous.tags + entry.tags))
            merged['categories'] = list(
                dict.fromkeys(previous.categories + entry.categories)
            )
            for field in (
                'name',
                'display_name',
                'range',
                'description',
                'style',
            ):
                old, new = getattr(previous, field), getattr(entry, field)
                if old is None:
                    merged[field] = new
                elif new is not None and old != new:
                    errors.append(f'{context}: conflicting {field} for {key}')
            entries[key] = models.TablePatch.model_validate(merged)
    return list(entries.values())


def _text(
    value: str, width: int, target: models.Target, context: str, errors: list[str]
) -> bytes:
    unsupported = list(
        dict.fromkeys(
            c for c in value if c not in target.characters or not 32 <= ord(c) <= 126
        )
    )
    if unsupported:
        errors.append(f'{context}: unsupported characters {unsupported!r}')
    if not value or len(value) >= width:
        errors.append(f'{context}: text must contain 1..{width - 1} characters')
    # Placeholder bytes let later entries be checked. Errors prevent emission.
    return value.encode('ascii', errors='replace')[: width - 1]


def _optional(value: int | None) -> int:
    return 255 if value is None else value


HEADER = struct.Struct('<4sHHIHHHHII')
METADATA = struct.Struct('<4sHH')
DOCUMENT_VERSION = 14
