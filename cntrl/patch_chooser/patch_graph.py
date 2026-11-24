"""Compile the executable patch graph, including bindings and presentation metadata."""

import re
import struct
from collections.abc import Mapping
from pathlib import Path

from pydantic import ValidationError

from .. import source_files
from . import (
    banked,
    device_settings,
    display,
    graphics,
    inputs,
    midi_endpoints,
    midi_messages,
    models,
    page_settings,
    relay_outputs,
)
from .compiler_errors import (
    CompilationError,
    validation_errors,
)
from .compiler_inputs import (
    instrument_from_source,
    join_tables,
)
from .compiler_wire import (
    BINDING_HOOKS_OFFSET,
    BINDING_RECORD_SIZE,
    BINDING_SHIFTED_OFFSET,
    GESTURE_EVENTS,
    GESTURE_TIMINGS,
    GESTURES,
    GRAPH,
    LOOPER_ACTIONS,
    NAMED,
    SHIFTED_ACTIONS,
    STATE_OPERATIONS,
    SWITCH,
    SWITCH_HOOKS,
    fixed_text,
    optional_byte,
)


def normalize_patch(value: object) -> models.Patch:
    """Parse a patch operation or a named patch reference."""
    return models.patch_adapter.validate_python(value)


def display_template(text: str, properties: dict[str, str], path: str) -> str:
    if '\uf000' in text or any('\uf000' in v for v in properties.values()):
        raise CompilationError([f'{path}: reserved display character'])

    def expand(match: re.Match[str]) -> str:
        token = match[0]
        if token in ('{{', '}}'):
            return token[0]
        field = token[1:-1]
        if field == 'tempo':
            return '\uf000'
        if field.startswith('counters.'):
            return token
        if field in properties:
            return properties[field]
        raise CompilationError(
            [f'{path}: unknown or malformed template field {token!r}']
        )

    return re.sub(r'{{|}}|\{[^{}]+\}|\{', expand, text)


def resolve_midi_routing(
    value: models.Patch, instruments: Mapping[str, object], path: str
) -> models.Patch:
    if isinstance(value, models.ProgramChange) and value.instrument is None:
        if len(instruments) != 1:
            raise CompilationError(
                [
                    f'{path}: program requires exactly one instrument '
                    'or an explicit instrument'
                ]
            )
        return value.model_copy(update={'instrument': next(iter(instruments))})
    if isinstance(value, models.OutputMessage):
        missing = []
        if value.output is None:
            missing.append('output')
        if isinstance(value, models.ChannelMessage) and value.channel is None:
            missing.append('channel')
        if missing:
            if len(instruments) != 1:
                raise CompilationError(
                    [
                        f'{path}: missing {", ".join(missing)} '
                        'requires exactly one instrument'
                    ]
                )
            alias, raw = next(iter(instruments.items()))
            try:
                instrument = instrument_from_source(alias, raw)
            except ValidationError as error:
                raise CompilationError(
                    validation_errors(f'instruments.{alias}', error)
                ) from error
            return value.model_copy(update={k: getattr(instrument, k) for k in missing})
    return value


def resolve_derivation(
    value: models.Patch,
    definitions: dict[str, object],
    path: str,
    parents: tuple[str, ...] = (),
) -> models.ConcretePatch:
    if isinstance(value, models.Sequence):
        return value.model_copy(
            update={
                'patches': [
                    resolve_derivation(p, definitions, path, parents)
                    for p in value.patches
                ]
            }
        )
    if not isinstance(value, models.Derived):
        return value
    key = value.parent
    if key in parents:
        raise CompilationError(
            [f'{path}: parent cycle: {" -> ".join((*parents, key))}']
        )
    if key not in definitions:
        raise CompilationError([f'{path}: unknown parent {key!r}'])
    try:
        parent = resolve_derivation(
            normalize_patch(definitions[key]), definitions, path, (*parents, key)
        )
        fields = parent.model_dump(exclude_unset=True) | (value.model_extra or {})
        if isinstance(fields.get('midi'), dict):
            fields['midi'] = (parent.midi or {}) | fields['midi']
        merged = type(parent).model_validate(fields)
    except ValidationError as error:
        raise CompilationError(validation_errors(path, error)) from error
    return resolve_derivation(merged, definitions, path, (*parents, key))


class PatchCompiler:
    """Resolve the single Patch graph once, before serializing executable nodes."""

    def __init__(
        self,
        show: models.Setup,
        profiles: dict[tuple[str, str], models.InstrumentProfile],
        entries: list[models.TablePatch],
        instruments: dict[str, models.Instrument],
        target: models.Target,
        errors: list[str],
        documents: dict[str, object],
        *,
        source_snapshots: dict[Path, bytes] | None = None,
    ) -> None:
        self.show, self.profiles, self.entries, self.instruments = (
            show,
            profiles,
            entries,
            instruments,
        )
        self.target, self.errors, self.documents = target, errors, documents
        self.source_snapshots = source_snapshots
        self.binding_images: list[str] = []
        self.counters = dict(show.counters)
        self.values: list[models.ConcretePatch] = []
        self.nodes: list[bytes] = []
        self.paths: list[str] = []
        self.labels: list[str] = []
        self.children: dict[int, list[int]] = {}
        self.named: dict[str, int] = {}
        self.programs: dict[
            int, tuple[bytes, int, str | None, str | None, display.DisplayPatch]
        ] = {}
        self.browser: list[tuple[int, str, str]] = []
        self.derivations: dict[int, dict[str, object]] = {}
        self.program_metadata: dict[int, dict[str, object]] = {}
        self.program_names: dict[
            tuple[str, str, int, int | None], list[str | None]
        ] = {}
        self.binding_assignments: list[
            tuple[str, models.SwitchAssignment, int, int]
        ] = []

    def parse(self, raw: object, path: str, name: str | None = None) -> int | None:
        if path.startswith('patch_cycles.') and type(raw) is int:
            if len(self.instruments) != 1:
                self.errors.append(
                    f'{path}: a numeric program requires exactly one instrument'
                )
                return None
            raw = {
                'type': 'program-change',
                'instrument': next(iter(self.instruments)),
                'program': raw,
            }
        try:
            raw = models.infer_patch_type(raw)
        except ValueError as error:
            self.errors.append(f'{path}: {error}')
            return None
        if (
            path.startswith('patch_cycles.')
            and isinstance(raw, dict)
            and 'type' not in raw
            and 'parent' not in raw
            and 'patch' not in raw
            and 'patches' not in raw
            and 'operation' not in raw
            and 'name' in raw
        ):
            matches = [
                (a, e)
                for a, i in self.instruments.items()
                for e in self.entries
                if e.instrument == i.instrument
                and e.profile == i.profile
                and e.name == raw['name']
                and ('instrument' not in raw or raw['instrument'] == a)
                and ('bank' not in raw or raw['bank'] == e.bank)
            ]
            if len(matches) != 1:
                self.errors.append(
                    f'{path}: expected one exact program match for {raw["name"]!r}, '
                    f'found {len(matches)}; qualify with instrument and bank'
                )
                return None
            alias, entry = matches[0]
            raw = raw | {
                'type': 'program-change',
                'instrument': alias,
                'program': entry.program,
                'bank': entry.bank,
            }
        try:
            value = normalize_patch(raw)
        except ValidationError as error:
            self.errors.extend(validation_errors(path, error))
            return None
        return self.add(value, path, name)

    def add(self, value: models.Patch, path: str, name: str | None = None) -> int:
        value = resolve_midi_routing(value, self.show.instruments, path)
        if isinstance(value, models.Derived):
            concrete = resolve_derivation(value, self.show.patches, path)
            index = self.add(concrete, path, name)
            metadata: dict[str, object] = {
                '_derived': {'parent': value.parent, **(value.model_extra or {})}
            }
            entry = index
            while self.nodes[entry][0] in (10, 12):
                entry = self.children[entry][0]
            if entry in self.programs:
                metadata['_derived_program'] = self.nodes[entry][4:].hex()
            self.derivations[index] = metadata
            return index
        for key in ('name', 'display_name'):
            if (text := getattr(value, key)) is not None:
                fixed_text(
                    text, 64, self.target, path + '.' + key, self.errors, unicode=True
                )
        index = len(self.nodes)
        self.nodes.append(b'')
        self.values.append(value)
        self.paths.append(path)
        if isinstance(value, models.Sequence):
            default_label = 'sequence'
        elif isinstance(value, models.Reference):
            default_label = 'reference'
        elif isinstance(
            value,
            (
                models.Brightness,
                models.UsbMidiInterface,
                models.GlobalSetting,
                models.Navigation,
                models.Shift,
            ),
        ):
            default_label = 'state'
        elif isinstance(
            value,
            (
                models.StockFirmware,
                models.PlayMidiFile,
                models.RecordMidiFile,
                models.MidiLooperAction,
                models.MidiStream,
                models.Relay,
                models.Keyboard,
                models.ScrollCounter,
                models.TapTempo,
            ),
        ):
            default_label = value.operation
        else:
            default_label = value.type
        self.labels.append(value.display_name or value.name or name or default_label)
        if value.midi is not None:
            child = self.add(
                value.model_copy(update={'midi': None}), path + '.entry', name
            )
            self.nodes[index] = struct.pack('<BBHH', 12, 0, 2, child)
            self.children[index] = [child]
            return index
        if value.scene_status is not None and value.bindings is None:
            raise CompilationError(
                [f'{path}.scene_status: declare bindings for the scene']
            )
        if value.bindings is not None or value.navigation is not None:
            if not self.target.max_switches:
                self.errors.append(f'{path}.bindings: target has no switch engine')
            child = self.add(
                value.model_copy(
                    update={
                        'bindings': None,
                        'binding_view': None,
                        'navigation': None,
                        'scene_status': None,
                    }
                ),
                path + '.entry',
                name,
            )
            payload = self.compile_bindings(value, child, path)
            inheritance = list(models.Inherits).index(value.inherits)
            self.nodes[index] = (
                struct.pack('<BBH', 10, inheritance, len(payload)) + payload
            )
            self.children[index] = [child]
            return index
        flags = 0
        if isinstance(value, models.Banked):
            local = {}
            templates = {}
            for key, raw in value.patches.items():
                context = f'{path}.patches.{key}'
                try:
                    page = resolve_derivation(
                        normalize_patch(raw), self.show.patches, context
                    )
                except ValidationError as error:
                    self.errors.extend(validation_errors(context, error))
                    continue
                page = page.model_copy(
                    update={
                        'style': page.style or value.style,
                        'binding_view': page.binding_view or value.binding_view,
                    }
                )
                child = self.add(page, context)
                if self.nodes[child][0] != 10:
                    self.errors.append(f'{context}: bank preset pages need bindings')
                local[key] = child
                templates[key] = page
            empty: dict[str | None, int] = {}
            for number in range(value.bank_count):
                bank = value.banks.get(number, {})
                if all(
                    p in bank
                    for p in range(value.page_counts.get(number, value.page_count))
                ):
                    continue
                key = bank[min(bank)] if bank else None
                if key not in empty and (key is None or key in templates):
                    empty[key] = self.add(
                        banked.empty_page(value, templates.get(key), self.target),
                        f'{path}.empty.{number}',
                    )
            self.children[index] = list(local.values()) + list(empty.values())
            flags = value.page_count
            kind, payload = 17, b''
            try:
                payload = banked.definition(value, local, empty, self.show, self.target)
            except ValueError as error:
                self.errors.append(f'{path}: {error}')
        elif isinstance(value, models.CounterMessage):
            if value.counter not in self.counters:
                self.errors.append(f'{path}: unknown counter {value.counter!r}')
            if value.output not in self.target.outputs:
                self.errors.append(f'{path}: unavailable output {value.output!r}')
            assert value.output is not None and value.channel is not None
            kind, payload = (
                16,
                struct.pack(
                    '<H32sBBB',
                    list(self.counters).index(value.counter)
                    if value.counter in self.counters
                    else 0,
                    fixed_text(
                        value.output, 32, self.target, path + '.output', self.errors
                    ),
                    value.channel,
                    ('none', 'increase', 'decrease').index(value.change),
                    value.wrap,
                ),
            )
        elif isinstance(value, models.ScrollCounter):
            if value.counter not in self.counters:
                self.errors.append(f'{path}: unknown counter {value.counter!r}')
            kind, payload = (
                14,
                struct.pack(
                    '<HB',
                    list(self.counters).index(value.counter)
                    if value.counter in self.counters
                    else 0,
                    value.direction == 'backward',
                ),
            )
        elif isinstance(value, models.MidiStream):
            reference = value.stream
            if reference.startswith('system/'):
                valid = reference[7:] in (self.show.midi or {})
            else:
                valid = any(reference in (p.midi or {}) for p in self.values)
                valid = valid or any(
                    reference
                    in (
                        resolve_derivation(
                            normalize_patch(p), self.show.patches, path
                        ).midi
                        or {}
                    )
                    for p in self.show.patches.values()
                )
            if not valid:
                self.errors.append(f'{path}.stream: unknown MIDI stream {reference!r}')
            kind, payload = (
                13,
                struct.pack(
                    '<72sB',
                    fixed_text(
                        reference, 72, self.target, path + '.stream', self.errors
                    ),
                    value.enabled,
                ),
            )
        elif isinstance(value, models.Keyboard):
            if 'usb-hid-keyboard' not in self.target.functions:
                self.errors.append(f'{path}: USB HID keyboard unavailable on target')
            keys = bytes(sorted(set(value.keys))).ljust(6, b'\0')
            kind, payload = 20, bytes((value.action == 'press', value.modifiers)) + keys
        elif isinstance(value, models.Relay):
            if value.relay not in self.target.relay_outputs:
                self.errors.append(f'{path}.relay: unsupported contact {value.relay!r}')
            kind, payload = (
                11,
                struct.pack(
                    '<32sBI',
                    fixed_text(
                        value.relay, 32, self.target, path + '.relay', self.errors
                    ),
                    relay_outputs.ACTIONS.index(value.action),
                    round((value.duration or 0) * 1000),
                ),
            )
        elif isinstance(value, models.Delay):
            kind, payload = 7, struct.pack('<I', round(value.duration * 1000))
        elif isinstance(value, models.PlayMidiFile):
            outputs = (
                value.outputs
                if value.outputs is not None
                else [self.target.default_midi_output]
            )
            routes = {
                e.name: e.route
                for e in self.target.midi_endpoints
                if midi_endpoints.enabled(e, self.show)
            }
            for output in outputs:
                if output not in routes:
                    self.errors.append(
                        f'{path}: unavailable MIDI file output {output!r}'
                    )
            kind = 8
            payload = struct.pack(
                '<96sHIIIdBb',
                value.filename.encode('ascii'),
                sum(1 << routes[n] for n in outputs if n in routes),
                value.loops,
                value.pre_delay,
                value.post_delay,
                value.speed,
                value.reverse,
                value.transpose,
            )
        elif isinstance(value, models.MidiLooperAction):
            if self.show.page_settings.looper is None and not any(
                p.type == 'midi-looper' for p in self.show.pages.values()
            ):
                self.errors.append(f'{path}: MIDI looper requires shared configuration')
            kind, payload = (
                15,
                struct.pack(
                    '<B96s',
                    LOOPER_ACTIONS.index(value.action),
                    value.filename.encode('ascii'),
                ),
            )
        elif isinstance(value, models.RecordMidiFile):
            kind = 9
            routes = {e.name: e.route for e in self.target.midi_endpoints if e.input}
            input_names = value.inputs if value.inputs is not None else list(routes)
            if not input_names:
                self.errors.append(f'{path}: target has no MIDI recording input_names')
            for name in input_names:
                if name not in routes:
                    self.errors.append(
                        f'{path}: unavailable MIDI recording input {name!r}'
                    )
            payload = struct.pack(
                '<HH',
                sum(1 << (c - 1) for c in value.channels),
                sum(1 << routes[n] for n in input_names if n in routes),
            )
        elif isinstance(value, models.ProgramChange):
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
            setup_names = sorted(self.documents, key=str.lower)
            setup_index = next(
                (
                    i
                    for i, n in enumerate(setup_names)
                    if value.document and n.lower() == value.document.lower()
                ),
                65535,
            )
            payload = struct.pack(
                '<H64s',
                setup_index,
                fixed_text(
                    value.patch,
                    64,
                    self.target,
                    path + '.patch',
                    self.errors,
                    unicode=True,
                ),
            )
        elif isinstance(value, models.StockFirmware):
            kind = 4
            try:
                mask = inputs.button_mask(value.exit_buttons, self.target)
            except ValueError as error:
                self.errors.append(f'{path}: {error}')
                mask = 0
            payload = bytes((mask,))
            if 'stock-firmware' not in self.target.functions:
                self.errors.append(f'{path}: stock firmware unavailable on target')
        elif isinstance(value, models.GlobalSetting):
            kind = 18
            try:
                field = device_settings.field_id(value.setting, self.target)
                encoded = (
                    0
                    if value.value is None
                    else device_settings.encode_value(field, value.value, self.target)
                )
                payload = struct.pack('<BBI', field, int(value.value is None), encoded)
            except ValueError as error:
                self.errors.append(f'{path}: {error}')
                payload = bytes(6)
        elif isinstance(
            value,
            (
                models.Brightness,
                models.UsbMidiInterface,
                models.Navigation,
                models.Shift,
                models.TapTempo,
            ),
        ):
            if isinstance(value, models.Shift) and not self.target.max_switches:
                self.errors.append(f'{path}: shift requires target switch support')
            kind = 3
            payload = struct.pack(
                '<BI',
                STATE_OPERATIONS[value.operation],
                int(getattr(value, 'value', 0)),
            )
        else:
            kind = 2
            assert value.output is not None
            if value.output not in self.target.outputs:
                self.errors.append(
                    f'{path}.output: unavailable output {value.output!r}'
                )
            if (
                (isinstance(value, models.ControlChange) and value.resolution == 14)
                or isinstance(value, models.Rpn)
                or (isinstance(value, models.Nrpn) and value.resolution == 7)
            ):
                flags = 10
                data = midi_messages.encode(value)
            elif isinstance(value, models.Nrpn):
                assert value.channel is not None
                flags = 4
                data = b''.join(
                    bytes((0xB0 | (value.channel - 1), c, v))
                    for c, v in (
                        (99, value.parameter >> 7),
                        (98, value.parameter & 127),
                        (6, value.value >> 7),
                        (38, value.value & 127),
                    )
                )
            else:
                flags = {'control-change': 0, 'pitch-bend': 2, 'sysex': 9}.get(
                    value.type, 1
                )
                data = midi_messages.encode(value)
            payload = (
                struct.pack(
                    '<32s',
                    fixed_text(
                        value.output, 32, self.target, path + '.output', self.errors
                    ),
                )
                + data
            )
        if len(payload) > 65535 or index >= 65535:
            self.errors.append(f'{path}: patch exceeds document storage capacity')
        self.nodes[index] = (
            struct.pack('<BBH', kind, flags, min(len(payload), 65535)) + payload
        )
        return index

    def compile_bindings(
        self, value: models.ConcretePatch, child: int, path: str
    ) -> bytes:
        records: list[bytes] = []
        visuals: list[bytes] = []
        scene_properties = {
            'name': value.name or self.labels[child],
            'display_name': self.labels[child],
        }
        expressions: dict[str, models.ExpressionAssignment] = {}
        binding_view = value.binding_view
        if binding_view is None and any(
            isinstance(v, models.SwitchBinding) and v.style and v.style.image
            for v in (value.bindings or {}).values()
        ):
            binding_view = models.BindingView(status=' ')
        navigation = (
            inputs.navigation_masks(
                value.navigation, self.target, inputs.NAVIGATION[2:]
            )
            if value.navigation
            else b''
        )

        def add_action(
            patch: models.Patch | None,
            suffix: str,
            context: str,
            assignment: models.SwitchAssignment,
        ) -> int:
            node = self.add(patch, context + suffix) if patch is not None else 65535
            if node != 65535:
                self.binding_assignments.append((context, assignment, node, 65535))
            return node

        def gestures(
            gesture: models.SwitchGestures | None,
            suffix: str,
            context: str,
            assignment: models.SwitchAssignment,
        ) -> bytes:
            if gesture is None:
                return GESTURES.pack(*([65535] * len(GESTURE_EVENTS)), 0, 0, 0, 0)
            return GESTURES.pack(
                *(
                    add_action(
                        getattr(gesture, e), f'{suffix}.{e}', context, assignment
                    )
                    for e in GESTURE_EVENTS
                ),
                *(round(getattr(gesture, t) * 1000) for t in GESTURE_TIMINGS),
            )

        for key, raw in (value.bindings or {}).items():
            binding = models.parse_binding(raw)
            context = f'{path}.bindings.{key}'
            if isinstance(binding, models.ExpressionAssignment):
                expressions[key] = binding
                continue
            try:
                button = inputs.index(key, self.show, self.target)
            except ValueError as error:
                self.errors.append(f'{context}: {error}')
                continue
            action = (
                binding
                if isinstance(binding, models.SwitchBinding)
                else models.SwitchBinding(on=binding)
            )
            assignment = models.SwitchAssignment(
                button=key, **action.model_dump(exclude_unset=True)
            )
            on = self.add(action.on, context + '.on')
            off = (
                self.add(action.off, context + '.off')
                if action.off is not None
                else 65535
            )
            self.binding_assignments.append((context, assignment, on, off))

            subject = self.values[on]
            if isinstance(subject, models.Reference) and not subject.document:
                if definition := self.show.patches.get(subject.patch):
                    subject = resolve_derivation(
                        normalize_patch(definition), self.show.patches, context
                    )
            properties = {
                'name': getattr(action.on, 'name', None)
                or subject.name
                or getattr(action.on, 'patch', key),
                'display_name': getattr(action.on, 'display_name', None)
                or subject.display_name
                or subject.name
                or getattr(action.on, 'patch', key),
            }
            scene_properties.update({f'{key}.{k}': v for k, v in properties.items()})
            authored_label = action.switch_label
            if authored_label is None and action.name is None:
                authored_label = (
                    getattr(action.on, 'switch_label', None) or subject.switch_label
                )
            legacy_label = (
                action.name
                or getattr(action.on, 'display_name', None)
                or getattr(action.on, 'name', None)
                or getattr(action.on, 'patch', None)
                or key
            )
            label = (
                display_template(authored_label, properties, context + '.switch_label')
                if authored_label is not None
                else legacy_label
            )
            records.append(
                SWITCH.pack(
                    button,
                    ('press', 'momentary', 'latched').index(action.mode),
                    on,
                    off,
                    fixed_text(
                        label,
                        64,
                        self.target,
                        context + '.name',
                        self.errors,
                        unicode=True,
                    ),
                    65534 if label != legacy_label else 65535,
                )
                + gestures(action.gestures, '.gestures', context, assignment)
                + SWITCH_HOOKS.pack(
                    add_action(
                        action.enter.action if action.enter else None,
                        '.enter.action',
                        context,
                        assignment,
                    ),
                    add_action(action.exit, '.exit', context, assignment),
                    action.enter.override if action.enter else False,
                )
                + (
                    struct.pack(
                        '<HH',
                        add_action(
                            action.shifted.on, '.shifted.on', context, assignment
                        ),
                        add_action(
                            action.shifted.off, '.shifted.off', context, assignment
                        ),
                    )
                    + gestures(
                        action.shifted.gestures,
                        '.shifted.gestures',
                        context,
                        assignment,
                    )
                    if action.shifted
                    else SHIFTED_ACTIONS.pack(
                        *([65535] * (2 + len(GESTURE_EVENTS))), 0, 0, 0, 0
                    )
                )
            )
            if view := binding_view:
                short = f'{key}: {label}' if view.show_labels else label
                visuals.append(
                    self.binding_graphics(short, action.style, view, view.maximum_size)
                    + self.binding_graphics(
                        action.display_name or label,
                        value.style,
                        view,
                        view.detail_size,
                    )
                    + fixed_text(
                        action.display_name or label,
                        64,
                        self.target,
                        context + '.display_name',
                        self.errors,
                        unicode=True,
                    ).ljust(64, b'\0')
                )
        settings = self.show.page_settings.model_copy(
            update={'expression': expressions}
        )
        expression = page_settings.pack_expression(
            self.show.model_copy(update={'page_settings': settings}),
            self.target,
            midi_endpoints.ports(self.target.midi_endpoints),
        )
        visual = b''
        status = (
            fixed_text(
                display_template(
                    value.scene_status, scene_properties, path + '.scene_status'
                ),
                64,
                self.target,
                path + '.scene_status',
                self.errors,
                unicode=True,
            ).ljust(64, b'\0')
            if value.scene_status is not None
            else b''
        )
        if view := binding_view:
            if len(records) > 6:
                raise ValueError(
                    f'{path}.binding_view: at most six switches are supported'
                )
            visual = (
                self.binding_graphics(
                    self.labels[child], value.style, view, view.detail_size
                )
                + self.binding_graphics(
                    view.status,
                    display.merge_style(value.style, view.status_style),
                    view,
                    view.status_size,
                )
                + bytes([int(view.align == 'switch') | int(view.show_labels) << 1])
                + fixed_text(
                    view.status,
                    64,
                    self.target,
                    path + '.binding_view.status',
                    self.errors,
                    unicode=True,
                ).ljust(64, b'\0')
                + b''.join(visuals)
            )
        return (
            struct.pack(
                '<H64sBB',
                child,
                fixed_text(
                    self.labels[child],
                    64,
                    self.target,
                    path + '.display_name',
                    self.errors,
                    unicode=True,
                ),
                len(records),
                (4 if navigation else 0)
                | (8 if status else 0)
                | (
                    1
                    if binding_view is not None
                    else 2
                    if value.style is not None
                    else 0
                ),
            )
            + (expression or bytes(4 * page_settings.EXPRESSION.size))
            + b''.join(records)
            + (
                visual
                if binding_view is not None
                else self.binding_graphics(' ', value.style, models.BindingView(), 16)
                if value.style is not None
                else b''
            )
            + status
            + navigation
        )

    def binding_graphics(
        self,
        text: str,
        style: models.Style | None,
        view: models.BindingView,
        maximum: int,
    ) -> bytes:
        from ..fonts import assets
        from ..fonts.outline import GLYPH

        authored = style
        style = display.merge_style(
            models.Style(
                font_name=view.font_name,
                foreground_color='white',
                background_color='black',
            ),
            style,
        )
        embedded = next(
            (f for f in self.target.fonts if f.name == style.font_name and f.id != 255),
            None,
        )
        if embedded is None:
            raise ValueError('binding views require an embedded controller font')
        assert style.font_name is not None
        if style.font_name not in self.target.font_sources:
            raise ValueError(
                f'binding view font lacks exported glyphs: {style.font_name}'
            )
        data = source_files.read(
            Path(self.target.font_sources[style.font_name]), self.source_snapshots
        )
        face = assets.decode(data, style.font_name, 0)
        entries = assets.entries(data)
        samples = [
            re.sub(
                r'\{counters\.([^}]+)\}',
                lambda m, d=d: d * len(str(self.counters[m[1]].maximum)),
                text.replace('{bank:03d}', '999')
                .replace('{page}', '4')
                .replace('\uf000', d * 3),
            )
            for d in '0123456789'
        ]
        used = {c for t in samples for c in t}

        def vertical_bounds(size: int) -> tuple[int, int]:
            # Embedded glyph positions use cap height minus the stored glyph top.
            fields, index, _ = entries[size]
            glyph_count, _, _ = struct.unpack('<HHH', bytes(fields[7:13]))
            glyphs = [
                GLYPH.unpack_from(index, i * GLYPH.size) for i in range(glyph_count)
            ]
            top = min(
                [
                    0,
                    *(
                        fields[14] - g[4]
                        for g in glyphs
                        if chr(g[0]) in used and g[1] and g[2]
                    ),
                ]
            )
            bottom = max(
                [
                    1,
                    *(
                        fields[14] - g[4] + g[2]
                        for g in glyphs
                        if chr(g[0]) in used and g[1] and g[2]
                    ),
                ]
            )
            return top, bottom

        size = next(
            (
                s
                for s in sorted(face.sizes, reverse=True)
                if s <= max(maximum, min(face.sizes))
                and vertical_bounds(s)[1] - vertical_bounds(s)[0]
                <= self.target.display_height // 3 - 4
                and max(graphics.bounds(t, face.sizes[s])[1] for t in samples)
                <= self.target.display_width - 8
            ),
            None,
        )
        if size is None:
            raise ValueError(f'binding text does not fit: {text!r}')
        foreground, background = graphics.colors(style, 0)
        top, bottom = vertical_bounds(size)
        packed = graphics.SCREEN.pack(
            embedded.id,
            size,
            0,
            8
            | sum(
                1 << i
                for i, n in enumerate(
                    ('foreground_color', 'background_color', 'font_name')
                )
                if authored is not None and getattr(authored, n) is not None
            )
            | (4 if 'font_name' in view.model_fields_set else 0),
            foreground,
            background,
            bottom - top,
            -top,
        )
        if style.image:
            if style.image not in self.binding_images:
                self.binding_images.append(style.image)
            packed = packed[:12] + struct.pack(
                '<I', self.binding_images.index(style.image) + 1
            )
        return packed

    def program(self, patch: models.ProgramChange, index: int, path: str) -> bytes:
        if (
            patch.instrument is None
            or (instrument := self.instruments.get(patch.instrument)) is None
        ):
            self.errors.append(
                f'{path}.instrument: unknown or invalid instrument {patch.instrument!r}'
            )
            return b'\0' * 11
        profile = self.profiles.get((instrument.instrument, instrument.profile))
        if profile is None:
            self.errors.append(f'{path}.instrument: unknown or invalid profile')
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
                and e.profile == instrument.profile
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
                    if (e.instrument, e.profile, e.program, e.bank)
                    == (instrument.instrument, instrument.profile, program, bank)
                ),
                None,
            )
        mapping = profile.banks.get(bank) if bank is not None else None
        if bank is not None and mapping is None:
            self.errors.append(
                f'{path}.bank: bank {bank} has no mapping in this profile'
            )
        if bank is not None and bank >= 0xFFFFFFFF:
            self.errors.append(f'{path}.bank: exceeds storage capacity')
        name = matched.name if matched and matched.name else patch.name
        program_name = matched.display_name if matched else None
        identity = (instrument.instrument, instrument.profile, program, bank)
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
                'names': matched.names if matched and matched.names else None,
                'style': matched.style.model_dump(exclude_none=True)
                if matched and matched.style
                else None,
                'tags': matched.tags if matched else [],
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
            optional_byte(mapping.msb if mapping else None),
            optional_byte(mapping.lsb if mapping else None),
            optional_byte(patch.volume),
            optional_byte(patch.pan + 64 if patch.pan is not None else None),
        )
        record = struct.pack(
            '<HHBBBBB3x64s',
            values[0],
            0,
            *values[1:],
            fixed_text(
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
                'patch_cycle.name': '',
                'display_name': label,
                'name': name,
                'program': program,
                'bank': bank,
                'volume': patch.volume,
                'pan': patch.pan,
                'instrument': patch.instrument,
                **(
                    {f'names.{k}': v for k, v in matched.names.items()}
                    if matched
                    else {}
                ),
            },
            style=patch.style,
            program_style=matched.style if matched else None,
            tags=patch.tags,
            program_tags=matched.tags if matched else [],
        )
        logical_bank = 0xFFFFFFFF if bank is None else min(bank, 0xFFFFFFFF)
        self.programs[index] = record, logical_bank, name, program_name, view
        return struct.pack('<HBBBBBI', *values, logical_bank)

    def inherited_tags(self, index: int, seen: set[int] | None = None) -> list[str]:
        seen = set() if seen is None else seen
        if index in seen:
            return []
        seen.add(index)
        inherited = [
            c
            for i in self.reachable_children(index)
            for c in self.inherited_tags(i, seen)
        ]
        if index in self.programs:
            inherited += self.programs[index][4].program_tags
        return list(dict.fromkeys(inherited + self.values[index].tags))

    def presentation(
        self, index: int, seen: set[int] | None = None
    ) -> tuple[int, models.Presentation]:
        seen = set() if seen is None else seen
        value = self.values[index]
        if self.nodes[index][0] in (10, 12) and index not in seen:
            seen.add(index)
            return self.presentation(
                struct.unpack_from('<H', self.nodes[index], 4)[0], seen
            )
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
            fields['tags'] = self.inherited_tags(index)
        elif isinstance(value, models.Reference) and value.document:
            documents = {k.casefold(): v for k, v in self.documents.items()}
            raw = documents.get(value.document.casefold())
            if (
                isinstance(raw, dict)
                and isinstance(named := raw.get('patches'), dict)
                and isinstance(target := named.get(value.patch), (dict, str))
            ):
                if isinstance(target, str):
                    target = {'patch': target}
                if 'parent' in target:
                    target = resolve_derivation(
                        normalize_patch(target), named, f'patches.{value.patch}'
                    ).model_dump(exclude_unset=True)
                for k in models.Presentation.model_fields:
                    if k not in value.model_fields_set and k in target:
                        fields[k] = target[k]
                self.labels[index] = str(
                    target.get('display_name') or target.get('name') or value.patch
                )
        return index, models.Presentation.model_validate(fields)

    def dependency_leaves(
        self, index: int
    ) -> list[tuple[models.Patch, str, models.Setup]]:
        """Resolve local and companion dependencies before any send."""
        leaves: list[tuple[models.Patch, str, models.Setup]] = []
        documents: dict[str, models.Setup] = {'': self.show}
        for key, raw in self.documents.items():
            try:
                documents[key.casefold()] = models.Setup.model_validate(raw)
            except ValidationError as error:
                self.errors.extend(validation_errors(key, error))
        pending: list[tuple[models.Patch, str, str, list[tuple[str, str]]]] = [
            (self.values[index], self.paths[index], '', [])
        ]
        while pending:
            value, path, document, chain = pending.pop()
            current = documents[document]
            if isinstance(value, models.Derived):
                value = resolve_derivation(value, current.patches, path)
            if (
                isinstance(value, (models.Sequence, models.Reference))
                and value.bindings is not None
            ):
                leaves.append((value, path, current))
            if isinstance(value, models.Sequence):
                pending.extend(
                    (p, f'{path}.patches[{i}]', document, chain)
                    for i, p in reversed(list(enumerate(value.patches)))
                )
                continue
            if not isinstance(value, models.Reference):
                value = resolve_midi_routing(value, current.instruments, path)
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
                    validation_errors(
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
        self, source: models.Setup, contexts: list['PatchCompiler']
    ) -> 'PatchCompiler':
        if found := next((c for c in contexts if c.show == source), None):
            return found
        profiles = dict(self.profiles)
        entries = self.entries
        if source.definitions:
            profiles.update(
                {(s.instrument, s.name): s for s in source.definitions.profiles}
            )
            entries = join_tables(
                [
                    {'patches': [e.model_dump() for e in self.entries]},
                    {'patches': [p.model_dump() for p in source.definitions.programs]},
                ],
                self.errors,
            )
        entries = join_tables(
            [
                {'patches': [e.model_dump() for e in entries]},
                {'instruments': source.instruments},
            ],
            self.errors,
        )
        instruments: dict[str, models.Instrument] = {}
        for key, raw in source.instruments.items():
            try:
                instrument = instrument_from_source(key, raw)
                instruments[key] = instrument
                if instrument.profile == 'default':
                    profiles.setdefault(
                        (instrument.instrument, instrument.profile),
                        models.InstrumentProfile(
                            instrument=instrument.instrument, name='default'
                        ),
                    )
            except ValidationError as error:
                self.errors.extend(validation_errors(f'instruments.{key}', error))
        result = PatchCompiler(
            source,
            profiles,
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
        assignments = assignments + self.binding_assignments
        leaves = [self.dependency_leaves(i) for i in range(len(self.nodes))]
        for root, _, path in self.browser:
            if any(isinstance(v, models.Shift) for v, _, _ in leaves[root]):
                self.errors.append(
                    f'{path}: shift operations require a switch assignment'
                )
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
                if (
                    assignment.mode != 'press'
                    or assignment.gestures is not None
                    or assignment.enter is not None
                    or assignment.exit is not None
                    or assignment.shifted is not None
                ) and (
                    isinstance(value, (models.StockFirmware, models.Banked))
                    or getattr(value, 'bindings', None) is not None
                    or isinstance(value, models.Navigation)
                    and value.operation
                    in ('back', 'next-song-step', 'previous-song-step')
                ):
                    self.errors.append(
                        f'switches.{key}: {path} leaves the switch context; '
                        'only press assignments support this operation'
                    )
                if isinstance(value, models.ProgramChange):
                    raw = (
                        source.instruments.get(value.instrument)
                        if value.instrument is not None
                        else None
                    )
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
                elif isinstance(value, models.PlayMidiFile):
                    resources.update(
                        (o, 0, -1)
                        for o in (
                            value.outputs
                            if value.outputs is not None
                            else [self.target.default_midi_output]
                        )
                    )
                elif isinstance(value, models.OutputMessage):
                    output = value.output
                    assert output is not None
                    if isinstance(
                        value,
                        (
                            models.ControlChange,
                            models.ChannelProgramChange,
                            models.PitchBend,
                            models.Nrpn,
                            models.Rpn,
                        ),
                    ):
                        channel = value.channel
                        assert channel is not None
                        if isinstance(value, models.ControlChange):
                            resources.add((output, channel, value.controller))
                            if value.resolution == 14:
                                resources.add((output, channel, value.controller + 32))
                        elif isinstance(value, models.ChannelProgramChange):
                            resources.add((output, channel, 128))
                        elif isinstance(value, models.PitchBend):
                            resources.add((output, channel, 129))
                        else:
                            controls = (
                                (99, 98, 6, 38)
                                if isinstance(value, models.Nrpn)
                                and value.resolution == 14
                                else (99, 98, 6, 38, 101, 100)
                            )
                            resources.update((output, channel, c) for c in controls)
                    else:
                        resources.add((output, 0, -1))
            for previous, mode, other in ownership:
                if (
                    key.rsplit('.bindings.', 1)[:-1]
                    != previous.rsplit('.bindings.', 1)[:-1]
                ):
                    continue
                if key == previous or assignment.mode == mode == 'press':
                    continue
                if any(
                    o == p and (c == 0 or d == 0)
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

    def inline_references(self) -> None:
        contexts: list[PatchCompiler] = [self]
        imported: dict[tuple[int, int], int] = {}
        reference_names: dict[tuple[int, int], str] = {}
        counter_names: dict[tuple[int, str], str] = {}

        def reference(
            context: PatchCompiler, value: models.Reference
        ) -> tuple[PatchCompiler, int]:
            if value.document:
                source = next(
                    v
                    for k, v in self.documents.items()
                    if k.casefold() == value.document.casefold()
                )
                context = self.source_context(
                    models.Setup.model_validate(source), contexts
                )
            if value.patch not in context.named:
                node = context.parse(
                    context.show.patches[value.patch],
                    f'patches.{value.patch}',
                    value.patch,
                )
                assert node is not None
                context.named[value.patch] = node
            return context, context.named[value.patch]

        def import_node(context: PatchCompiler, node: int) -> int:
            if context is self:
                return node
            key = contexts.index(context), node
            if key in imported:
                return imported[key]
            destination = len(self.nodes)
            if destination >= 65535:
                raise CompilationError(['inlined patches exceed node capacity'])
            imported[key] = destination
            self.nodes.append(b'')
            self.values.append(context.values[node])
            self.paths.append(context.paths[node])
            self.labels.append(context.labels[node])
            data = bytearray(context.nodes[node])
            if data[0] in (14, 16):
                source_name = list(context.counters)[
                    struct.unpack_from('<H', data, 4)[0]
                ]
                identity = contexts.index(context), source_name
                if identity not in counter_names:
                    name = source_name
                    if name in self.counters:
                        name = f'inline_{identity[0]}_{len(self.counters)}'
                        while name in self.counters:
                            name = '_' + name
                    counter_names[identity] = name
                    self.counters[name] = context.counters[source_name]
                name = counter_names[identity]
                self.values[destination] = context.values[node].model_copy(
                    update={'counter': name}
                )
                struct.pack_into('<H', data, 4, list(self.counters).index(name))
            if data[0] == 1:
                source_alias, instrument = list(context.instruments.items())[
                    struct.unpack_from('<H', data, 4)[0]
                ]
                profile = context.profiles[instrument.instrument, instrument.profile]
                alias = next(
                    (
                        a
                        for a, i in self.instruments.items()
                        if i == instrument
                        and self.profiles[i.instrument, i.profile] == profile
                    ),
                    None,
                )
                if alias is None:
                    alias = source_alias
                    if alias in self.instruments:
                        number = len(self.instruments)
                        while (alias := f'inline_{number}') in self.instruments:
                            number += 1
                    if (
                        existing := self.profiles.get(
                            (instrument.instrument, instrument.profile)
                        )
                    ) is not None and existing != profile:
                        number = 0
                        while (
                            instrument.instrument,
                            name := f'inline_{number}',
                        ) in self.profiles:
                            number += 1
                        profile = profile.model_copy(update={'name': name})
                        instrument = instrument.model_copy(update={'profile': name})
                    self.profiles[instrument.instrument, instrument.profile] = profile
                    self.instruments[alias] = instrument
                struct.pack_into('<H', data, 4, list(self.instruments).index(alias))
                self.program_metadata[destination] = context.program_metadata[node]
            if data[0] == 6:
                value = context.values[node]
                assert isinstance(value, models.Reference)
                source, target = reference(context, value)
                child = import_node(source, target)
                identity = contexts.index(source), target
                if identity not in reference_names:
                    name = value.patch
                    if name in self.named and self.named[name] != child:
                        name = f'inline_{identity[0]}_{target}'
                        while name in self.named:
                            name = '_' + name
                    reference_names[identity] = name
                    self.named[name] = child
                name = reference_names[identity]
                data = bytearray(
                    struct.pack('<BBHH64s', 6, 0, 66, 65535, name.encode('utf-8'))
                )
                self.values[destination] = value.model_copy(
                    update={'patch': name, 'document': None}
                )
            else:
                offsets = []
                if data[0] == 5:
                    offsets = list(range(6, len(data), 2))
                elif data[0] == 12:
                    offsets = [4]
                elif data[0] == 10:
                    offsets = [4] + [
                        224 + i * BINDING_RECORD_SIZE + j
                        for i in range(data[70])
                        for j in (
                            *range(2, 6, 2),
                            *range(72, 72 + len(GESTURE_EVENTS) * 2, 2),
                            BINDING_HOOKS_OFFSET,
                            BINDING_HOOKS_OFFSET + 2,
                            *range(
                                BINDING_SHIFTED_OFFSET,
                                BINDING_SHIFTED_OFFSET + 2 * (2 + len(GESTURE_EVENTS)),
                                2,
                            ),
                        )
                    ]
                    count, style = data[70], data[71] & 3
                    visual = 224 + count * BINDING_RECORD_SIZE
                    graphics_offsets = [visual] if style else []
                    if style == 1:
                        graphics_offsets += [visual + 16]
                        graphics_offsets += [
                            visual + 97 + i * 96 + shift
                            for i in range(count)
                            for shift in (0, 16)
                        ]
                    for address in graphics_offsets:
                        image = struct.unpack_from('<I', data, address + 12)[0]
                        if image:
                            name = context.binding_images[image - 1]
                            if name not in self.binding_images:
                                self.binding_images.append(name)
                            struct.pack_into(
                                '<I',
                                data,
                                address + 12,
                                self.binding_images.index(name) + 1,
                            )
                for offset in offsets:
                    child = struct.unpack_from('<H', data, offset)[0]
                    if child != 65535:
                        struct.pack_into(
                            '<H', data, offset, import_node(context, child)
                        )
                if data[0] in (5, 12):
                    self.children[destination] = [
                        struct.unpack_from('<H', data, o)[0] for o in offsets
                    ]
            self.nodes[destination] = bytes(data)
            return destination

        def leaves(context: PatchCompiler, node: int) -> list[int]:
            data = context.nodes[node]
            if data[0] == 6:
                value = context.values[node]
                assert isinstance(value, models.Reference)
                return leaves(*reference(context, value))
            if data[0] == 5:
                return [
                    i
                    for child in context.children[node]
                    for i in leaves(context, child)
                ]
            return [import_node(context, node)]

        for index, value in enumerate(list(self.values)):
            if (
                not isinstance(value, models.Reference)
                or not value.document
                or self.nodes[index][0] != 6
            ):
                continue
            children = leaves(*reference(self, value))
            payload = struct.pack('<H', len(children)) + b''.join(
                struct.pack('<H', c) for c in children
            )
            self.nodes[index] = struct.pack('<BBH', 5, 0, len(payload)) + payload
            self.children[index] = children

    def display_metadata(self) -> dict[str, object]:
        return {
            str(i): {
                k: v
                for k, v in p.model_dump(exclude_unset=True, exclude_none=True).items()
                if k in models.Presentation.model_fields and k != 'bindings'
            }
            | (
                {'pickup': 'none'}
                if 'pickup' in p.model_fields_set and p.pickup is None
                else {}
            )
            | (
                {
                    'bindings': {
                        k: models.parse_binding(v).model_dump(exclude_none=True)
                        for k, v in p.bindings.items()
                    }
                }
                if p.bindings is not None and self.nodes[i][0] == 10
                else {}
            )
            | (
                {'_program': self.program_metadata[i]}
                if i in self.program_metadata
                else {}
            )
            | self.derivations.get(i, {})
            | (
                {'_document': p.document}
                if isinstance(p, models.Reference) and p.document
                else {}
            )
            for i, p in enumerate(self.values)
        }

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
            + b''.join(NAMED.pack(k.encode('utf-8'), v) for k, v in self.named.items())
            + b''.join(self.nodes)
        )
