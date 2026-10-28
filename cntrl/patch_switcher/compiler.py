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
        self.errors = list(dict.fromkeys(errors))
        super().__init__(
            'Configuration errors:\n' + '\n'.join(f'- {e}' for e in self.errors)
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
    """Parse only the explicit tagged authoring representation."""
    return models.patch_adapter.validate_python(value)


def compile_show(
    document: object,
    definitions: object,
    tables: list[object],
    target: models.Target,
    assets: dict[int, bytes] | None = None,
    documents: dict[str, object] | None = None,
) -> bytes:
    errors: list[str] = []
    generated: dict[int, bytes] = {}
    try:
        show = models.Show.model_validate(document)
    except ValidationError as error:
        raise CompilationError(_validation_errors('show', error)) from error
    try:
        parsed = show.definitions or models.Definitions.model_validate(
            definitions if definitions else {'setups': []}
        )
    except ValidationError as error:
        raise CompilationError(_validation_errors('definitions', error)) from error
    setups: dict[tuple[str, str], models.Setup] = {}
    for s in parsed.setups:
        key = (s.instrument, s.name)
        if key in setups:
            errors.append(f'definitions: duplicate setup {key}')
        setups[key] = s
        if any(b < 0 for b in s.banks):
            errors.append(f'definitions: negative bank in {key}')
    entries = _join_tables(
        [{'patches': [p.model_dump() for p in parsed.programs]}, *tables], errors
    )
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
    compiler = PatchCompiler(
        show, setups, entries, instruments, target, errors, documents or {}
    )
    named: dict[str, int] = {}
    for key, raw in show.patches.items():
        _text(key, 64, target, f'patches.{key}', errors)
        if (index := compiler.parse(raw, f'patches.{key}', key)) is not None:
            named[key] = index
    categories: list[bytes] = []
    records: list[bytes] = []
    banks: list[int] = []
    names: list[bytes] = []
    roots: list[int] = []
    views: list[display.DisplayPatch] = []
    for category, raw in show.groups.items():
        label = _text(category, 64, target, f'groups.{category}', errors, unicode=True)
        if not isinstance(raw, list):
            errors.append(f'groups.{category}: expected a list of Patch values')
            continue
        if not raw:
            errors.append(f'groups.{category}: group is empty')
        start = len(roots)
        for i, value in enumerate(raw):
            path = f'groups.{category}[{i}]'
            if (index := compiler.parse(value, path)) is None:
                continue
            roots.append(index)
            compiler.browser.append((index, category, path))
        categories.append(
            struct.pack(
                '<64sHH', label, min(start, 65535), min(len(roots) - start, 65535)
            )
        )
    switch_records: list[bytes] = []
    switch_views: list[display.DisplayPatch] = []
    assignments: list[tuple[str, models.SwitchAssignment, int, int]] = []
    buttons: set[str] = set()
    for key, raw in show.switches.items():
        path = f'switches.{key}'
        try:
            assignment = models.SwitchAssignment.model_validate(raw)
        except ValidationError as error:
            errors.extend(_validation_errors(path, error))
            continue
        if assignment.button in buttons:
            errors.append(f'{path}.button: duplicate switch button {assignment.button}')
        if target.buttons and assignment.button not in target.buttons:
            errors.append(f'{path}.button: unavailable on target')
        buttons.add(assignment.button)
        on = compiler.add(assignment.on, path + '.on')
        off = (
            compiler.add(assignment.off, path + '.off')
            if assignment.off is not None
            else 65535
        )
        assignments.append((key, assignment, on, off))
        name = key if assignment.name is None else assignment.name
        switch_records.append(
            SWITCH.pack(
                ord(assignment.button) - ord('A'),
                ('press', 'momentary', 'latched').index(assignment.mode),
                on,
                off,
                _text(name, 64, target, path + '.name', errors, unicode=True),
                0,
            )
        )
        switch_views.append(
            display.DisplayPatch(
                context=path,
                values={
                    'group.name': 'Switches',
                    'display_name': name,
                    'switch.name': f'{assignment.button}: {name}',
                    'switch.mode': assignment.mode,
                    'switch.button': assignment.button,
                    'switch.state': 'Unknown remote state',
                },
                style=assignment.style,
            )
        )
    compiler.named = named
    compiler.validate(assignments)
    for index, category, path in compiler.browser:
        patch, presentation = compiler.presentation(index)
        label = presentation.display_name or presentation.name or compiler.labels[patch]
        view = display.DisplayPatch(
            context=path,
            values={
                'group.name': category,
                'display_name': label,
                'name': presentation.name,
                'program': None,
                'bank': None,
                'volume': None,
                'pan': None,
                'instrument': None,
            },
            style=presentation.style,
            categories=presentation.categories,
        )
        program = compiler.programs.get(patch)
        if program is not None:
            record, bank, name, program_name, program_view = program
            record = bytearray(record)
            # Group index is presentation data; node execution does not read it.
            group_index = next(
                i
                for i, c in enumerate(categories)
                if c[:64].split(b'\0')[0].decode('utf-8') == category
            )
            struct.pack_into('<H', record, 2, group_index)
            record[12:76] = _text(
                label, 64, target, path + '.display_name', errors, unicode=True
            ).ljust(64, b'\0')
            view = program_view.model_copy(
                update={
                    'context': path,
                    'style': presentation.style,
                    'categories': presentation.categories,
                    'values': program_view.values
                    | {'group.name': category, 'display_name': label},
                }
            )
            records.append(bytes(record))
            banks.append(bank)
            texts = (name, program_name, presentation.display_name)
        else:
            value = compiler.values[patch]
            mask = value.button_mask if isinstance(value, models.StockFirmware) else 0
            records.append(
                struct.pack(
                    '<HHBBBBBBBB64s',
                    65535,
                    next(
                        i
                        for i, c in enumerate(categories)
                        if c[:64].split(b'\0')[0].decode('utf-8') == category
                    ),
                    0,
                    255,
                    255,
                    255,
                    255,
                    int(bool(mask)),
                    mask,
                    0,
                    _text(
                        label, 64, target, path + '.display_name', errors, unicode=True
                    ),
                )
            )
            banks.append(0xFFFFFFFF)
            texts = (presentation.name, None, presentation.display_name)
        names.extend(_name_record(t, path, errors) for t in texts)
        views.append(view)
    for label, count, maximum in (
        ('instruments', len(instruments), target.max_instruments),
        ('groups', len(categories), target.max_categories),
        ('patches', len(roots), target.max_patches),
        ('switches', len(assignments), target.max_switches),
    ):
        if count > maximum:
            errors.append(f'{label}: {count} exceeds target capacity {maximum}')
    try:
        screen_records, references, display_records = display.compile_display(
            show,
            views + switch_views,
            target,
            generated,
            {
                str(i): {
                    k: v
                    for k, v in p.model_dump(
                        exclude_unset=True, exclude_none=True
                    ).items()
                    if k in models.Presentation.model_fields
                }
                | (
                    {'_program': compiler.program_metadata[i]}
                    if i in compiler.program_metadata
                    else {}
                )
                for i, p in enumerate(compiler.values)
            },
        )
    except (OSError, ValueError) as error:
        raise CompilationError(errors + [str(error)]) from error
    if generated:
        from ..maker.morningstar.mc.font_assets import validate_budget

        validate_budget(screen_records, display_records, generated)
    if assets is not None:
        assets.update(generated)
    records = [r + struct.pack('<3H', *references[i]) for i, r in enumerate(records)]
    switch_records = [
        r[:-2] + struct.pack('<H', references[len(records) + i][1])
        for i, r in enumerate(switch_records)
    ]
    mappings: list[bytes] = []
    identities: list[bytes] = []
    for i, (alias, instrument) in enumerate(instruments.items()):
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
        for bank, mapping in setups[
            (instrument.instrument, instrument.setup)
        ].banks.items():
            if not 0 <= bank < 0xFFFFFFFF:
                errors.append(f'instruments.{alias}: bank exceeds storage capacity')
                continue
            mappings.append(
                struct.pack(
                    '<HIBB', i, bank, _optional(mapping.msb), _optional(mapping.lsb)
                )
            )
    if len(mappings) > 65535:
        errors.append('too many bank mappings')
    metadata = METADATA.pack(
        b'CMTA', min(len(mappings), 65535), show.transition_ms or 0
    )
    body = b''.join(screen_records + instrument_records + categories + records)
    body += (
        metadata
        + b''.join(identities + mappings)
        + b''.join(struct.pack('<I', b) for b in banks)
    )
    body += (
        b'CNAM'
        + b''.join(names)
        + compiler.pack(roots, switch_records)
        + display_records
    )
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
            len(roots),
            show.brightness | int(show.splash) << 8 | int(show.usb_midi_interface) << 9,
            crc32(body),
            show.screensaver_timeout * 1000,
        )
        + body
    )


class PatchCompiler:
    """Resolve the single Patch graph once, before serializing executable nodes."""

    def __init__(
        self,
        show: models.Show,
        setups: dict[tuple[str, str], models.Setup],
        entries: list[models.TablePatch],
        instruments: dict[str, models.Instrument],
        target: models.Target,
        errors: list[str],
        documents: dict[str, object],
    ) -> None:
        self.show, self.setups, self.entries, self.instruments = (
            show,
            setups,
            entries,
            instruments,
        )
        self.target, self.errors, self.documents = target, errors, documents
        self.values: list[models.Patch] = []
        self.nodes: list[bytes] = []
        self.paths: list[str] = []
        self.labels: list[str] = []
        self.children: dict[int, list[int]] = {}
        self.named: dict[str, int] = {}
        self.programs: dict[
            int, tuple[bytes, int, str | None, str | None, display.DisplayPatch]
        ] = {}
        self.browser: list[tuple[int, str, str]] = []
        self.program_metadata: dict[int, dict[str, object]] = {}
        self.program_names: dict[
            tuple[str, str, int, int | None], list[str | None]
        ] = {}

    def parse(self, raw: object, path: str, name: str | None = None) -> int | None:
        try:
            value = normalize_patch(raw)
        except ValidationError as error:
            self.errors.extend(_validation_errors(path, error))
            return None
        return self.add(value, path, name)

    def add(self, value: models.Patch, path: str, name: str | None = None) -> int:
        for key in ('name', 'display_name'):
            if (text := getattr(value, key)) is not None:
                _text(
                    text, 64, self.target, path + '.' + key, self.errors, unicode=True
                )
        if unknown := set(value.categories) - set(self.show.categories):
            self.errors.append(
                f'{path}.categories: unknown categories {sorted(unknown)}'
            )
        index = len(self.nodes)
        self.nodes.append(b'')
        self.values.append(value)
        self.paths.append(path)
        self.labels.append(value.display_name or value.name or name or value.type)
        flags = 0
        if isinstance(value, models.ProgramChange):
            kind, payload = 1, self.program(value, index, path)
        elif isinstance(value, models.Sequence):
            kind = 5
            children = [
                self.add(p, f'{path}.patches[{i}]') for i, p in enumerate(value.patches)
            ]
            self.children[index] = children
            payload = struct.pack('<H', len(children)) + b''.join(
                struct.pack('<H', c) for c in children
            )
        elif isinstance(value, models.Reference):
            kind = 6
            payload = struct.pack(
                '<64s64s',
                _text(value.document, 64, self.target, path + '.document', self.errors)
                if value.document
                else b'',
                _text(value.patch, 64, self.target, path + '.patch', self.errors),
            )
        elif isinstance(value, models.StockFirmware):
            kind, payload = 4, bytes((value.button_mask,))
            if 'stock-firmware' not in self.target.functions or any(
                c not in self.target.buttons for c in value.exit_buttons.split('+')
            ):
                self.errors.append(
                    f'{path}: stock firmware or exit buttons unavailable on target'
                )
        elif isinstance(
            value, (models.Brightness, models.UsbMidiInterface, models.Navigation)
        ):
            kind = 3
            payload = struct.pack(
                '<BI',
                STATE_OPERATIONS[value.operation],
                int(getattr(value, 'value', 0)),
            )
        else:
            kind = 2
            if value.output not in self.target.outputs:
                self.errors.append(
                    f'{path}.output: unavailable output {value.output!r}'
                )
            channel = getattr(value, 'channel', 1) - 1
            if isinstance(value, models.ControlChange):
                data = bytes((0xB0 | channel, value.controller, value.value))
            elif isinstance(value, models.PitchBend):
                flags = 2
                bend = value.value + 8192
                data = bytes((0xE0 | channel, bend & 127, bend >> 7))
            elif isinstance(value, models.Nrpn):
                flags = 4
                data = b''.join(
                    bytes((0xB0 | channel, c, v))
                    for c, v in (
                        (99, value.parameter >> 7),
                        (98, value.parameter & 127),
                        (6, value.value >> 7),
                        (38, value.value & 127),
                    )
                )
            elif isinstance(value, models.SysEx):
                flags = 9
                data = bytes((0xF0, *value.data, 0xF7))
            else:
                flags = 1
                data = bytes(value.bytes)
                if not midi_message_valid(data):
                    self.errors.append(
                        f'{path}.bytes: expected one complete MIDI message '
                        'with an explicit status and valid data bytes'
                    )
            payload = (
                struct.pack(
                    '<32s',
                    _text(value.output, 32, self.target, path + '.output', self.errors),
                )
                + data
            )
        if len(payload) > 65535 or index >= 65535:
            self.errors.append(f'{path}: patch exceeds document storage capacity')
        self.nodes[index] = (
            struct.pack('<BBH', kind, flags, min(len(payload), 65535)) + payload
        )
        return index

    def program(self, patch: models.ProgramChange, index: int, path: str) -> bytes:
        instrument = self.instruments.get(patch.instrument)
        if instrument is None:
            self.errors.append(
                f'{path}.instrument: unknown or invalid instrument {patch.instrument!r}'
            )
            return b'\0' * 11
        setup = self.setups.get((instrument.instrument, instrument.setup))
        if setup is None:
            self.errors.append(f'{path}.instrument: unknown or invalid setup')
            return b'\0' * 11
        if instrument.output not in self.target.outputs:
            self.errors.append(
                f'{path}.instrument: unavailable output {instrument.output!r}'
            )
        program, bank = patch.program, patch.bank
        matched = None
        if isinstance(program, str):
            try:
                pattern = re.compile(program)
            except re.error as error:
                self.errors.append(f'{path}.program: invalid regex: {error}')
                return b'\0' * 11
            matches = [
                e
                for e in self.entries
                if e.instrument == instrument.instrument
                and e.setup == instrument.setup
                and (bank is None or e.bank == bank)
                and e.name is not None
                and pattern.search(e.name)
            ]
            if not matches:
                self.errors.append(f'{path}.program: no patch matches {program!r}')
                return b'\0' * 11
            if len(matches) > 1 and not self.show.take_first_match:
                self.errors.append(
                    f'{path}.program: multiple matches for {program!r}: '
                    + ', '.join(
                        f'{e.name} (PC {e.program}, bank {e.bank})' for e in matches
                    )
                )
                return b'\0' * 11
            matched = matches[0]
            program, bank = matched.program, matched.bank
        else:
            matched = next(
                (
                    e
                    for e in self.entries
                    if (e.instrument, e.setup, e.program, e.bank)
                    == (instrument.instrument, instrument.setup, program, bank)
                ),
                None,
            )
        if matched and (unknown := set(matched.categories) - set(self.show.categories)):
            self.errors.append(
                f'{path}.program.categories: unknown categories {sorted(unknown)}'
            )
        mapping = setup.banks.get(bank) if bank is not None else None
        if bank is not None and mapping is None:
            self.errors.append(f'{path}.bank: bank {bank} has no mapping in this setup')
        if bank is not None and bank >= 0xFFFFFFFF:
            self.errors.append(f'{path}.bank: exceeds storage capacity')
        name = matched.name if matched and matched.name else patch.name
        program_name = matched.display_name if matched else None
        identity = (instrument.instrument, instrument.setup, program, bank)
        names = [name, program_name]
        if identity in self.program_names and self.program_names[identity] != names:
            self.errors.append(f'{path}: conflicting program names for {identity}')
        self.program_names[identity] = names
        self.program_metadata[index] = {
            k: v
            for k, v in {
                'program': program,
                'bank': bank,
                'name': name,
                'display_name': program_name,
                'style': matched.style.model_dump(exclude_none=True)
                if matched and matched.style
                else None,
                'categories': matched.categories if matched else [],
            }.items()
            if v is not None
        }

        if (
            matched
            and matched.name
            and patch.name is not None
            and patch.name != matched.name
        ):
            self.errors.append(
                f'{path}.name: name differs from synthesizer name {matched.name!r}; '
                'use display_name to override it'
            )
        label = (
            patch.display_name
            or program_name
            or name
            or (f'PC {program}' if bank is None else f'Bank {bank} / PC {program}')
        )
        if patch.display_name or program_name or name:
            self.labels[index] = label
        elif self.labels[index] == patch.type:
            self.labels[index] = label
        values = (
            list(self.instruments).index(patch.instrument),
            program - 1,
            _optional(mapping.msb if mapping else None),
            _optional(mapping.lsb if mapping else None),
            _optional(patch.volume),
            _optional(patch.pan + 64 if patch.pan is not None else None),
        )
        record = struct.pack(
            '<HHBBBBB3x64s',
            values[0],
            0,
            *values[1:],
            _text(
                label,
                64,
                self.target,
                path + '.display_name',
                self.errors,
                unicode=True,
            ),
        )
        view = display.DisplayPatch(
            context=path,
            values={
                'group.name': '',
                'display_name': label,
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
        logical_bank = 0xFFFFFFFF if bank is None else min(bank, 0xFFFFFFFF)
        self.programs[index] = record, logical_bank, name, program_name, view
        return struct.pack('<HBBBBBI', *values, logical_bank)

    def inherited_categories(
        self, index: int, seen: set[int] | None = None
    ) -> list[str]:
        seen = set() if seen is None else seen
        if index in seen:
            return []
        seen.add(index)
        inherited = [
            c
            for i in self.reachable_children(index)
            for c in self.inherited_categories(i, seen)
        ]
        if index in self.programs:
            inherited += self.programs[index][4].program_categories
        return list(dict.fromkeys(inherited + self.values[index].categories))

    def presentation(
        self, index: int, seen: set[int] | None = None
    ) -> tuple[int, models.Presentation]:
        seen = set() if seen is None else seen
        value = self.values[index]
        if (
            index not in seen
            and isinstance(value, models.Reference)
            and not value.document
            and value.patch in self.named
        ):
            seen.add(index)
            target, presentation = self.presentation(self.named[value.patch], seen)
            fields = presentation.model_dump()
            fields.update(
                {
                    k: v
                    for k, v in value.model_dump(exclude_unset=True).items()
                    if k in models.Presentation.model_fields
                }
            )
            if value.style is not None:
                fields['style'] = display.merge_style(
                    presentation.style, value.style
                ).model_dump(exclude_none=True)
            return target, models.Presentation.model_validate(fields)
        fields = {k: getattr(value, k) for k in models.Presentation.model_fields}
        if isinstance(value, models.Sequence):
            fields['categories'] = self.inherited_categories(index)
        elif isinstance(value, models.Reference) and value.document:
            documents = {k.casefold(): v for k, v in self.documents.items()}
            raw = documents.get(value.document.casefold())
            if (
                isinstance(raw, dict)
                and isinstance(named := raw.get('patches'), dict)
                and isinstance(target := named.get(value.patch), dict)
            ):
                for k in models.Presentation.model_fields:
                    if k not in value.model_fields_set and k in target:
                        fields[k] = target[k]
                self.labels[index] = str(
                    target.get('display_name') or target.get('name') or value.patch
                )
        return index, models.Presentation.model_validate(fields)

    def dependency_leaves(
        self, index: int
    ) -> list[tuple[models.Patch, str, models.Show]]:
        """Resolve local and companion dependencies before any send."""
        leaves: list[tuple[models.Patch, str, models.Show]] = []
        documents: dict[str, models.Show] = {'': self.show}
        for key, raw in self.documents.items():
            try:
                documents[key.casefold()] = models.Show.model_validate(raw)
            except ValidationError as error:
                self.errors.extend(_validation_errors(key, error))
        pending: list[tuple[models.Patch, str, str, list[tuple[str, str]]]] = [
            (self.values[index], self.paths[index], '', [])
        ]
        while pending:
            value, path, document, chain = pending.pop()
            current = documents[document]
            if isinstance(value, models.Sequence):
                pending.extend(
                    (p, f'{path}.patches[{i}]', document, chain)
                    for i, p in reversed(list(enumerate(value.patches)))
                )
                continue
            if not isinstance(value, models.Reference):
                leaves.append((value, path, current))
                continue
            destination = value.document.casefold() if value.document else document
            if destination not in documents:
                self.errors.append(
                    f'{path}.document: setup {value.document!r} does not exist; '
                    'supply its companion document'
                )
                continue
            source = documents[destination]
            if source == self.show:
                destination = ''
            token = destination, value.patch
            if token in chain:
                self.errors.append(
                    'reference cycle: '
                    + ' -> '.join(
                        f'{d + ":" if d else ""}patches.{k}' for d, k in chain + [token]
                    )
                )
                continue
            if value.patch not in source.patches:
                self.errors.append(
                    f'{path}: patch {value.patch!r} does not exist in '
                    f'{destination or "this setup"}'
                )
                continue
            try:
                child = normalize_patch(source.patches[value.patch])
            except ValidationError as error:
                self.errors.extend(
                    _validation_errors(
                        f'{destination + ":" if destination else ""}'
                        f'patches.{value.patch}',
                        error,
                    )
                )
                continue
            pending.append(
                (
                    child,
                    f'{destination + ":" if destination else ""}patches.{value.patch}',
                    destination,
                    chain + [token],
                )
            )
        return leaves

    def source_context(
        self, source: models.Show, contexts: list['PatchCompiler']
    ) -> 'PatchCompiler':
        if found := next((c for c in contexts if c.show == source), None):
            return found
        setups = dict(self.setups)
        entries = self.entries
        if source.definitions:
            setups.update(
                {(s.instrument, s.name): s for s in source.definitions.setups}
            )
            entries = _join_tables(
                [
                    {'patches': [e.model_dump() for e in self.entries]},
                    {'patches': [p.model_dump() for p in source.definitions.programs]},
                ],
                self.errors,
            )
        instruments: dict[str, models.Instrument] = {}
        for key, raw in source.instruments.items():
            try:
                instruments[key] = models.Instrument.model_validate(raw)
            except ValidationError as error:
                self.errors.extend(_validation_errors(f'instruments.{key}', error))
        result = PatchCompiler(
            source,
            setups,
            entries,
            instruments,
            self.target,
            self.errors,
            self.documents,
        )
        contexts.append(result)
        return result

    def validate(
        self, assignments: list[tuple[str, models.SwitchAssignment, int, int]]
    ) -> None:
        leaves = [self.dependency_leaves(i) for i in range(len(self.nodes))]
        contexts: list[PatchCompiler] = [self]
        resolved: dict[int, tuple[PatchCompiler, int]] = {}
        for dependencies in leaves:
            for value, path, source in dependencies:
                context = self.source_context(source, contexts)
                index = next(
                    (i for i, p in enumerate(context.values) if p == value), None
                )
                if index is None:
                    index = context.add(value, path)
                resolved[id(value)] = context, index
        ownership: list[tuple[str, str, set[tuple[str, int, int]]]] = []
        for key, assignment, on, off in assignments:
            resources: set[tuple[str, int, int]] = set()
            for value, path, source in leaves[on] + (
                [] if off == 65535 else leaves[off]
            ):
                if assignment.mode != 'press' and (
                    isinstance(value, models.StockFirmware)
                    or isinstance(value, models.Navigation)
                    and value.operation in ('menu', 'back')
                ):
                    self.errors.append(
                        f'switches.{key}: {path} leaves the switch context; '
                        'only press assignments support this operation'
                    )
                if isinstance(value, models.ProgramChange):
                    raw = source.instruments.get(value.instrument)
                    try:
                        instrument = models.Instrument.model_validate(raw)
                    except ValidationError:
                        continue
                    resources.add((instrument.output, instrument.channel, 128))
                    context, index = resolved[id(value)]
                    if program := context.programs.get(index):
                        for controller, offset in ((0, 5), (32, 6), (7, 7), (10, 8)):
                            if program[0][offset] != 255:
                                resources.add(
                                    (instrument.output, instrument.channel, controller)
                                )
                elif isinstance(value, (models.Midi, models.SysEx)):
                    resources.add((value.output, 0, -1))
                elif isinstance(value, models.ControlChange):
                    resources.add((value.output, value.channel, value.controller))
                elif isinstance(value, models.PitchBend):
                    resources.add((value.output, value.channel, 129))
                elif isinstance(value, models.Nrpn):
                    resources.update(
                        (value.output, value.channel, c) for c in (99, 98, 6, 38)
                    )
            for previous, mode, other in ownership:
                if assignment.mode == mode == 'press':
                    continue
                if any(
                    o == p and (c == 0 or d == 0 or c == d and r == s)
                    for o, c, r in resources
                    for p, d, s in other
                ):
                    self.errors.append(
                        f'switches.{key}: MIDI ownership conflicts '
                        f'with switches.{previous}'
                    )
            ownership.append((key, assignment.mode, resources))

    def reachable_children(self, index: int) -> list[int]:
        value = self.values[index]
        if (
            isinstance(value, models.Reference)
            and not value.document
            and value.patch in self.named
        ):
            return [self.named[value.patch]]
        return self.children.get(index, [])

    def pack(self, roots: list[int], switches: list[bytes]) -> bytes:
        prefix = (
            GRAPH.size
            + len(self.nodes) * 4
            + len(roots) * 2
            + len(switches) * SWITCH.size
            + len(self.named) * NAMED.size
        )
        offsets: list[int] = []
        for n in self.nodes:
            offsets.append(prefix)
            prefix += len(n)
        return (
            GRAPH.pack(
                b'CPAT',
                prefix,
                len(self.nodes),
                len(switches),
                len(roots),
                len(self.named),
            )
            + b''.join(struct.pack('<I', o) for o in offsets)
            + b''.join(struct.pack('<H', r) for r in roots)
            + b''.join(switches)
            + b''.join(NAMED.pack(k.encode('ascii'), v) for k, v in self.named.items())
            + b''.join(self.nodes)
        )


def midi_message_valid(data: bytes) -> bool:
    """Require one explicitly framed MIDI message, without inherited running status."""
    if not data or data[0] < 0x80:
        return False
    status = data[0]
    if status == 0xF0:
        return len(data) >= 2 and data[-1] == 0xF7 and all(b < 128 for b in data[1:-1])
    length = (
        2
        if 0xC0 <= status < 0xE0
        else 3
        if status < 0xF0
        else {
            0xF1: 2,
            0xF2: 3,
            0xF3: 2,
            0xF6: 1,
            0xF8: 1,
            0xFA: 1,
            0xFB: 1,
            0xFC: 1,
            0xFE: 1,
            0xFF: 1,
        }.get(status, 0)
    )
    return len(data) == length and all(b < 128 for b in data[1:])


def _name_record(text: str | None, path: str, errors: list[str]) -> bytes:
    if text is None:
        return struct.pack('<H', 65535)
    encoded = text.encode('utf-8')
    if len(encoded) >= 65535:
        errors.append(f'{path}: name exceeds storage capacity')
    return struct.pack('<H', min(len(encoded), 65534)) + encoded[:65534]


GRAPH = struct.Struct('<4sIHHHH')
SWITCH = struct.Struct('<BBHH64sH')
NAMED = struct.Struct('<64sH')
STATE_OPERATIONS = {
    n: i + 1
    for i, n in enumerate(
        (
            'brightness',
            'usb-midi-interface',
            'next-patch',
            'previous-patch',
            'next-group',
            'previous-group',
            'activate-selection',
            'undo',
            'menu',
            'back',
        )
    )
}


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
    value: str,
    width: int,
    target: models.Target,
    context: str,
    errors: list[str],
    unicode: bool = False,
) -> bytes:
    unsupported = list(
        dict.fromkeys(
            c
            for c in value
            if ord(c) < 32
            or ord(c) == 127
            or (not unicode and (c not in target.characters or ord(c) > 126))
        )
    )
    if unsupported:
        errors.append(f'{context}: unsupported characters {unsupported!r}')
    encoded = (
        value.encode('utf-8') if unicode else value.encode('ascii', errors='replace')
    )
    if not value or len(encoded) >= width:
        errors.append(f'{context}: text must contain 1..{width - 1} characters')
    # Placeholder bytes let later entries be checked. Errors prevent emission.
    return encoded[: width - 1]


def _optional(value: int | None) -> int:
    return 255 if value is None else value


HEADER = struct.Struct('<4sHHIHHHHII')
METADATA = struct.Struct('<4sHH')
DOCUMENT_VERSION = 17
