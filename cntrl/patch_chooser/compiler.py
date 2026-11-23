"""Compile a complete show, accumulating errors before producing any binary."""

import struct
from pathlib import Path
from zlib import crc32

from pydantic import ValidationError

from . import (
    arpeggiator,
    banked,
    counters,
    device_settings,
    display,
    inputs,
    layout,
    loop_reset,
    looper,
    midi_endpoints,
    midi_processing,
    models,
    page_settings,
    pickup,
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
    GESTURE_EVENTS,
    GESTURE_TIMINGS,
    GESTURES,
    GESTURES_FLAG,
    METADATA,
    SCROLL_DIRECTIONS,
    SHIFTED_ACTIONS,
    SWITCH,
    SWITCH_HOOKS,
    TRS_MODES,
    fixed_text,
    optional_byte,
    pack_document,
    pack_name,
)
from .patch_graph import PatchCompiler


def compile_show(
    document: object,
    definitions: object,
    tables: list[object],
    target: models.Target,
    assets: dict[int, bytes] | None = None,
    documents: dict[str, object] | None = None,
    movie_assets: dict[int, bytes] | None = None,
    *,
    source_snapshots: dict[Path, bytes] | None = None,
    setting_overrides: dict[str, object] | None = None,
    binding_scopes: list[tuple[int, str, dict[str, object]]] | None = None,
    scene_images: dict[int, tuple[str, models.Style, int]] | None = None,
) -> bytes:
    errors: list[str] = []
    generated: dict[int, bytes] = {}
    try:
        show = models.Setup.model_validate(document)
    except ValidationError as error:
        raise CompilationError(validation_errors('show', error)) from error
    if 'doom' not in target.functions:
        for name, page in show.pages.items():
            if page.type == 'doom':
                errors.append(f'pages.{name}: Doom is unavailable on this target')
    jacks = midi_endpoints.ports(target.midi_endpoints)
    for jack, mode in show.trs_ports.items():
        if jack not in jacks or mode not in target.trs_ports.get(jack, []):
            errors.append(f'trs_ports.{jack}: unsupported role {mode!r}')
    for jack in show.auxiliary_switches:
        if show.trs_ports.get(jack) != 'aux' or 'aux' not in target.trs_ports.get(
            jack, []
        ):
            errors.append(
                f'auxiliary_switches.{jack}: requires a supported aux TRS role'
            )
    target = target.model_copy(
        update={
            'midi_endpoints': [
                e.model_copy(
                    update={
                        'input': e.input
                        and (
                            e.port is None
                            or show.trs_ports.get(e.name) in ('midi-a', 'midi-b')
                        ),
                        'output': e.output
                        and (
                            e.port is None
                            or show.trs_ports.get(e.name) in ('midi-a', 'midi-b')
                        ),
                    }
                )
                for e in target.midi_endpoints
            ]
        }
    )
    if 'pages' not in show.model_fields_set:
        if show.regions:
            selected = {n for r in show.regions.values() for n in r.pages}
            pages = {n: p for n, p in show.pages.items() if n in selected}
            if 'None' in selected:
                pages['None'] = models.Page(type='None')
        else:
            pages = {
                n: p
                for n, p in show.pages.items()
                if p.min_displays <= len(target.displays)
            }
            if not looper.available_inputs(show, target) or not any(
                e.name == target.default_midi_output and midi_endpoints.enabled(e, show)
                for e in target.midi_endpoints
            ):
                if show.page_settings.looper is None:
                    pages.pop('midi-looper')
                if show.page_settings.arpeggiator is None:
                    pages.pop('arpeggiator')
            if 'doom' in target.functions:
                pages['doom'] = models.Page(type='doom')
            pages['None'] = models.Page(type='None')
        show = show.model_copy(
            update={
                'pages': pages,
                'layout': (
                    models.PageLayout(focus='tools')
                    if show.layout is None
                    and not show.regions
                    and len(target.displays) > 1
                    else show.layout
                ),
            }
        )
    try:
        parsed = show.definitions or models.Definitions.model_validate(
            definitions if definitions else {'profiles': []}
        )
    except ValidationError as error:
        raise CompilationError(validation_errors('definitions', error)) from error
    profiles: dict[tuple[str, str], models.InstrumentProfile] = {}
    for s in parsed.profiles:
        key = (s.instrument, s.name)
        if key in profiles:
            errors.append(f'definitions: duplicate profile {key}')
        profiles[key] = s
        if any(b < 0 for b in s.banks):
            errors.append(f'definitions: negative bank in {key}')
    entries = join_tables(
        [
            {'patches': [p.model_dump() for p in parsed.programs]},
            {'instruments': show.instruments},
            *tables,
        ],
        errors,
    )
    instruments: dict[str, models.Instrument] = {}
    instrument_records: list[bytes] = []
    for alias, raw in show.instruments.items():
        try:
            instrument = instrument_from_source(alias, raw)
        except ValidationError as error:
            errors.extend(validation_errors(f'instruments.{alias}', error))
            continue
        if (instrument.instrument, instrument.profile) not in profiles:
            if instrument.profile == 'default':
                profiles[instrument.instrument, instrument.profile] = (
                    models.InstrumentProfile(
                        instrument=instrument.instrument, name=instrument.profile
                    )
                )
            else:
                errors.append(f'instruments.{alias}: unknown instrument/profile')
        if instrument.output not in target.outputs:
            errors.append(
                f'instruments.{alias}: unavailable output {instrument.output!r}'
            )
        instruments[alias] = instrument
    compiler = PatchCompiler(
        show,
        profiles,
        entries,
        instruments,
        target,
        errors,
        documents or {},
        source_snapshots=source_snapshots,
    )
    named: dict[str, int] = {}
    for key, raw in show.patches.items():
        fixed_text(key, 64, target, f'patches.{key}', errors, unicode=True)
        if (index := compiler.parse(raw, f'patches.{key}', key)) is not None:
            named[key] = index
    scopes = list(binding_scopes or [])
    if show.bindings:
        scopes.insert(0, (0, '', {'bindings': show.bindings}))
    for kind, name, bindings in scopes:
        if kind not in (0, 2) or len(name.encode()) >= 64:
            errors.append('binding scope names must contain fewer than 64 bytes')
            continue
        path = f'binding_scopes.{kind}.{name}'
        scope = dict(bindings, inherits='none')
        if midi := scope.pop('midi', None):
            # Keep the binding map at the root and store the route scope beneath it.
            scope['patches'] = [{'patches': [], 'midi': midi}]
        child = compiler.parse(scope, path, name or 'Global')
        if child is not None:
            marker = len(compiler.nodes)
            compiler.nodes.append(
                struct.pack('<BBHB64sH', 19, 0, 67, kind, name.encode(), child)
            )
            compiler.values.append(models.Sequence(patches=[]))
            compiler.paths.append(path)
            compiler.labels.append(name or 'Global')
            compiler.children[marker] = [child]
    patch_cycles: list[bytes] = []
    records: list[bytes] = []
    banks: list[int] = []
    names: list[bytes] = []
    roots: list[int] = []
    views: list[display.DisplayPatch] = []
    for patch_cycle, raw in show.patch_cycles.items():
        label = fixed_text(
            patch_cycle, 64, target, f'patch_cycles.{patch_cycle}', errors, unicode=True
        )
        if not isinstance(raw, list):
            errors.append(
                f'patch_cycles.{patch_cycle}: expected a list of Patch values'
            )
            continue
        if not raw:
            errors.append(f'patch_cycles.{patch_cycle}: patch cycle is empty')
        start = len(roots)
        for i, value in enumerate(raw):
            path = f'patch_cycles.{patch_cycle}[{i}]'
            if (index := compiler.parse(value, path)) is None:
                continue
            roots.append(index)
            compiler.browser.append((index, patch_cycle, path))
        patch_cycles.append(
            struct.pack(
                '<64sHH', label, min(start, 65535), min(len(roots) - start, 65535)
            )
        )
    switch_records: list[bytes] = []
    switch_views: list[display.DisplayPatch] = []
    assignments: list[tuple[str, models.SwitchAssignment, int, int]] = []
    gesture_records = [
        GESTURES.pack(*([65535] * len(GESTURE_EVENTS)), 0, 0, 0, 0) for _ in range(5)
    ]
    gesture_assignments: list[tuple[str, models.SwitchAssignment, int, int]] = []
    hook_records = [SWITCH_HOOKS.pack(65535, 65535, False) for _ in range(5)]
    shifted_records = [
        SHIFTED_ACTIONS.pack(*([65535] * (2 + len(GESTURE_EVENTS))), 0, 0, 0, 0)
        for _ in range(5)
    ]
    buttons: set[str] = set()
    for key, raw in show.switches.items():
        path = f'switches.{key}'
        try:
            assignment = models.SwitchAssignment.model_validate(raw)
        except ValidationError as error:
            errors.extend(validation_errors(path, error))
            continue
        if assignment.button in buttons:
            errors.append(f'{path}.button: duplicate switch button {assignment.button}')
        try:
            button = inputs.index(assignment.button, show, target)
        except ValueError as error:
            errors.append(f'{path}.button: {error}')
            continue
        if assignment.button == target.controls.back:
            errors.append(f'{path}.button: reserved for back navigation')
            continue
        slot = len(assignments)
        if slot >= 5:
            errors.append(
                f'{path}: setup format supports at most five switch assignments'
            )
            continue
        buttons.add(assignment.button)
        on = compiler.add(assignment.on, path + '.on')
        off = (
            compiler.add(assignment.off, path + '.off')
            if assignment.off is not None
            else 65535
        )
        assignments.append((key, assignment, on, off))
        enter = (
            compiler.add(assignment.enter.action, path + '.enter.action')
            if assignment.enter is not None
            else 65535
        )
        exit = (
            compiler.add(assignment.exit, path + '.exit')
            if assignment.exit is not None
            else 65535
        )
        hook_records[slot] = SWITCH_HOOKS.pack(
            enter,
            exit,
            assignment.enter.override if assignment.enter is not None else False,
        )
        for node in (enter, exit):
            if node != 65535:
                gesture_assignments.append((key, assignment, node, 65535))
        if assignment.gestures is not None:
            gesture = assignment.gestures
            events: list[int] = []
            for event in GESTURE_EVENTS:
                value = getattr(gesture, event)
                node = (
                    compiler.add(value, f'{path}.gestures.{event}')
                    if value is not None
                    else 65535
                )
                events.append(node)
                if node != 65535:
                    gesture_assignments.append((key, assignment, node, 65535))
            gesture_records[slot] = GESTURES.pack(
                *events, *(round(getattr(gesture, t) * 1000) for t in GESTURE_TIMINGS)
            )
        if assignment.shifted is not None:
            shifted = assignment.shifted
            shifted_on = compiler.add(shifted.on, path + '.shifted.on')
            shifted_off = (
                compiler.add(shifted.off, path + '.shifted.off')
                if shifted.off is not None
                else 65535
            )
            gesture_assignments.append((key, assignment, shifted_on, shifted_off))
            events = [65535] * len(GESTURE_EVENTS)
            timings = [0] * 4
            if shifted.gestures is not None:
                gesture = shifted.gestures
                timings = [round(getattr(gesture, t) * 1000) for t in GESTURE_TIMINGS]
                for i, event in enumerate(GESTURE_EVENTS):
                    if (value := getattr(gesture, event)) is not None:
                        node = compiler.add(value, f'{path}.shifted.gestures.{event}')
                        events[i] = node
                        gesture_assignments.append((key, assignment, node, 65535))
            shifted_records[slot] = SHIFTED_ACTIONS.pack(
                shifted_on, shifted_off, *events, *timings
            )
        name = key if assignment.name is None else assignment.name
        switch_records.append(
            SWITCH.pack(
                button,
                ('press', 'momentary', 'latched').index(assignment.mode),
                on,
                off,
                fixed_text(name, 64, target, path + '.name', errors, unicode=True),
                0,
            )
        )
        switch_views.append(
            display.DisplayPatch(
                context=path,
                values={
                    'patch_cycle.name': 'Switches',
                    'display_name': name,
                    'switch.name': f'{assignment.button}: {name}'
                    if assignment.button in target.buttons
                    else name,
                    'switch.mode': assignment.mode,
                    'switch.button': assignment.button,
                    'switch.state': 'Unknown remote state',
                },
                style=assignment.style,
            )
        )
    compiler.named = named
    compiler.validate(assignments + gesture_assignments)
    for index, patch_cycle, path in compiler.browser:
        patch, presentation = compiler.presentation(index)
        label = presentation.display_name or presentation.name or compiler.labels[patch]
        value = compiler.values[index]
        identity = presentation.name or (
            value.patch if isinstance(value, models.Reference) else label
        )
        autostyle = (
            (identity, presentation.seed) if presentation.seed is not None else None
        )
        view = display.DisplayPatch(
            context=path,
            values={
                'patch_cycle.name': patch_cycle,
                'display_name': label,
                'name': presentation.name,
                'program': None,
                'bank': None,
                'volume': None,
                'pan': None,
                'instrument': None,
            },
            style=presentation.style,
            autostyle=autostyle,
            tags=presentation.tags,
        )
        program = compiler.programs.get(patch)
        if program is not None:
            record, bank, name, program_name, program_view = program
            record = bytearray(record)
            # Patch cycle index is presentation data; node execution does not read it.
            patch_cycle_index = next(
                i
                for i, c in enumerate(patch_cycles)
                if c[:64].split(b'\0')[0].decode('utf-8') == patch_cycle
            )
            struct.pack_into('<H', record, 2, patch_cycle_index)
            record[12:76] = fixed_text(
                label, 64, target, path + '.display_name', errors, unicode=True
            ).ljust(64, b'\0')
            view = program_view.model_copy(
                update={
                    'context': path,
                    'style': presentation.style,
                    'autostyle': autostyle,
                    'tags': presentation.tags,
                    'values': program_view.values
                    | {'patch_cycle.name': patch_cycle, 'display_name': label},
                }
            )
            records.append(bytes(record))
            banks.append(bank)
            texts = (name, program_name, presentation.display_name)
        else:
            value = compiler.values[patch]
            mask = (
                compiler.nodes[patch][4]
                if isinstance(value, models.StockFirmware)
                else 0
            )
            records.append(
                struct.pack(
                    '<HHBBBBBBBB64s',
                    65535,
                    next(
                        i
                        for i, c in enumerate(patch_cycles)
                        if c[:64].split(b'\0')[0].decode('utf-8') == patch_cycle
                    ),
                    0,
                    255,
                    255,
                    255,
                    255,
                    int(bool(mask)),
                    mask,
                    0,
                    fixed_text(
                        label, 64, target, path + '.display_name', errors, unicode=True
                    ),
                )
            )
            banks.append(0xFFFFFFFF)
            texts = (presentation.name, None, presentation.display_name)
        names.extend(pack_name(t, path, errors) for t in texts)
        views.append(view)
    if show.inline_patches and not errors:
        compiler.inline_references()
    show = show.model_copy(update={'counters': compiler.counters})
    for alias, instrument in instruments.items():
        instrument_records.append(
            struct.pack(
                '<32s32sB3x',
                fixed_text(alias, 32, target, f'instruments.{alias}', errors),
                fixed_text(
                    instrument.output, 32, target, f'instruments.{alias}.output', errors
                ),
                instrument.channel - 1,
            )
        )
    for label, count, maximum in (
        ('instruments', len(instruments), target.max_instruments),
        ('patch_cycles', len(patch_cycles), target.max_patch_cycles),
        ('patches', len(roots), target.max_patches),
        ('switches', len(assignments), target.max_switches),
    ):
        if count > maximum:
            errors.append(f'{label}: {count} exceeds target capacity {maximum}')
    try:
        from ..animation.movies import prepare

        show, movie_records = prepare(
            show,
            movie_assets if movie_assets is not None else {},
            target.animation,
            source_snapshots=source_snapshots,
        )
        screen_records, references, display_records = display.compile_display(
            show,
            views + switch_views,
            target,
            generated,
            compiler.display_metadata(),
            source_snapshots=source_snapshots,
            include_artwork=scene_images is None,
            binding_images=compiler.binding_images,
        )
        packed_pages = page_settings.pack(show, target)
        clock = page_settings.pack_clock(show, target)
        expression = page_settings.pack_expression(show, target, jacks)
        looper_settings = looper.pack(show, target)
        arpeggiator_settings = arpeggiator.pack(show, target)
        page_layout = layout.pack(
            show,
            [target.compiled_display_ids.get(d.id, d.id) for d in target.displays],
            inputs.navigation_masks(show.navigation, target),
        )
        banked_pages = banked.pack(show, named, compiler.nodes)
        relay_settings = relay_outputs.pack(show, target)
        counter_settings = counters.pack(compiler.counters, show, target)
        stream_settings = midi_processing.compile_routes(
            show.midi,
            {i: p.midi for i, p in enumerate(compiler.values) if p.midi is not None},
            target.midi_endpoints,
        )
    except (OSError, ValueError) as error:
        raise CompilationError(errors + [str(error)]) from error
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
                fixed_text(
                    instrument.instrument,
                    32,
                    target,
                    f'instruments.{alias}.instrument',
                    errors,
                ),
                fixed_text(
                    instrument.profile,
                    32,
                    target,
                    f'instruments.{alias}.profile',
                    errors,
                ),
            )
        )
        for bank, mapping in profiles[
            (instrument.instrument, instrument.profile)
        ].banks.items():
            if not 0 <= bank < 0xFFFFFFFF:
                errors.append(f'instruments.{alias}: bank exceeds storage capacity')
                continue
            mappings.append(
                struct.pack(
                    '<HIBB',
                    i,
                    bank,
                    optional_byte(mapping.msb),
                    optional_byte(mapping.lsb),
                )
            )
    if len(mappings) > 65535:
        errors.append('too many bank mappings')
    from .data_files import validate_name

    setup_name_bytes = bytearray()
    for name in sorted(documents or {}, key=str.lower):
        try:
            validate_name(name)
        except ValueError as error:
            errors.append(str(error))
        else:
            setup_name_bytes.extend(name.lower().encode('ascii').ljust(64, b'\0'))
    metadata = METADATA.pack(
        b'CMTA',
        min(len(mappings), 65535),
        round(show.scrolling.horizontal_duration * 1000),
        round(show.target_pulse.period * 1000),
        show.target_pulse.minimum,
        show.target_pulse.maximum,
        round(show.navigation.repeat_interval * 1000),
        round(show.target_pulse.delay * 1000),
        *packed_pages,
        bytes(TRS_MODES.index(show.trs_ports.get(j, 'off')) for j in jacks),
        round(show.scrolling.vertical_duration * 1000),
        *(
            SCROLL_DIRECTIONS.index(d)
            for d in (
                show.scrolling.send,
                show.scrolling.patches,
                show.scrolling.cycles,
            )
        ),
        int(show.fast_switches)
        | (int(not show.forward_device_to_host) << 1)
        | sum(
            1 << (i + 2)
            for i, j in enumerate(jacks)
            if show.auxiliary_switches.get(j) == 'combined'
        ),
        show.frame_buffers,
        int(show.use_animations),
        sum(p.ram2_bytes for p in show.pages.values())
        + display.estimate_frame_ram(show, target)
        + display.estimate_font_ram(screen_records, display_records, generated)
        + layout.estimate_ram(
            show.pages,
            len(instruments),
            len(patch_cycles),
            regions=page_layout[7],
        )
        + midi_processing.estimate_ram(
            stream_settings, endpoint_count=len(target.midi_endpoints)
        )
        + looper.estimate_ram(looper_settings)
        + arpeggiator.estimate_ram(arpeggiator_settings),
        crc32(setup_name_bytes),
    )
    body = b''.join(screen_records + instrument_records + patch_cycles + records)
    body += (
        metadata
        + b''.join(identities + mappings)
        + b''.join(struct.pack('<I', b) for b in banks)
    )
    gestures = (
        b''.join(gesture_records)
        if any(a.gestures is not None for _, a, _, _ in assignments)
        else b''
    )
    body += (
        expression
        + clock
        + gestures
        + (
            b'CHOK' + b''.join(hook_records)
            if any(
                a.enter is not None or a.exit is not None for _, a, _, _ in assignments
            )
            else b''
        )
        + (
            b'CSHF' + b''.join(shifted_records)
            if any(a.shifted is not None for _, a, _, _ in assignments)
            else b''
        )
        + page_layout
        + banked_pages
        + movie_records
        + midi_endpoints.pack(target)
        + inputs.pack(target)
        + relay_settings
        + counter_settings
        + stream_settings
        + pickup.pack(
            show.pickup,
            show.pickup_pairing_ms,
            [compiler.presentation(i)[1].pickup for i in range(len(compiler.values))],
        )
        + loop_reset.pack(show, target)
        + looper_settings
        + arpeggiator_settings
        + b'CNAM'
        + b''.join(names)
        + compiler.pack(roots, switch_records)
        + display_records
    )
    if errors:
        raise CompilationError(errors)
    if scene_images is not None:
        for i, node in enumerate(compiler.nodes):
            if node[0] != 10:
                continue
            target_node, presentation = compiler.presentation(i)
            program = compiler.programs.get(target_node)
            program_style = program[4] if program else None
            style = display.merge_style(
                show.style,
                display.tag_style(
                    (program_style.program_tags if program_style else [])
                    + presentation.tags,
                    show.tag_styles,
                ),
                program_style.program_style if program_style else None,
                presentation.style,
            )
            if style.image:
                scene_images[i] = (
                    node[6:70].split(b'\0')[0].decode(),
                    style,
                    crc32(node),
                )
    result = pack_document(
        show,
        body,
        instruments=len(instruments),
        cycles=len(patch_cycles),
        roots=len(roots),
        flags=(
            (page_settings.EXPRESSION_FLAG if expression else 0)
            | (page_settings.CLOCK_FLAG if clock else 0)
            | (GESTURES_FLAG if gestures else 0)
        ),
    )
    return (
        device_settings.attach(result, setting_overrides, target)
        if setting_overrides is not None
        else result
    )
