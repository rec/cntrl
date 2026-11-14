"""Perform the one-time extraction of the original MIDI stream engine."""

import re
from pathlib import Path

import tyro
from pydantic import BaseModel


class Split(BaseModel, frozen=True):
    root: Path = Path(__file__).resolve().parents[1]
    original: Path | None = None

    def run(self) -> None:
        directory = self.root / 'firmware/include/cntrl'
        if self.original is not None:
            finish(directory, self.original.read_text())
            return
        source = (directory / 'midi_processing.h').read_text()
        if 'class Streams : public ParameterObserver' not in source:
            raise SystemExit(
                'The original Streams declaration is absent; '
                'refusing to overwrite the split engine.'
            )
        split(directory, source)


def block(source: str, marker: str) -> tuple[int, int]:
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if source[end : end + 1] == ';':
        end += 1
    return start, end


def split(directory: Path, source: str) -> None:
    # The extraction deliberately uses the checked-in source as its only input.
    # Further maintenance belongs in the extracted headers, not this migration.
    def take(marker: str) -> str:
        nonlocal source
        start, end = block(source, marker)
        result = source[start:end]
        source = source[:start] + source[end:]
        return result

    emit = take('    struct Emit {')
    processor = take('    struct Processor {')
    field = take('    struct Field {')
    assignment = take('    struct Assignment {')
    step = take('    struct Step {')
    input_record = take('    struct Input {')
    route = take('    struct Route {')
    # These value records contain no advanced engines. They may be shared by
    # the queue and the optional policy without instantiating advanced state.
    records = {}
    for name in ('Work', 'ParameterBinding', 'ParameterDelivery', 'Capture', 'Visit'):
        records[name] = take(f'    struct {name} {{')
    common = '\n'.join(
        (
            emit,
            processor,
            field,
            assignment,
            input_record,
            records['Work'],
            records['ParameterBinding'],
            records['ParameterDelivery'],
            records['Capture'],
            records['Visit'],
        )
    )
    common = common.replace('    struct ', 'struct ')
    basic_step = """struct Step {
        uint8_t kind = 0, field = 0;
        CheckedArray<Field> match;
        CheckedArray<Assignment> assignments;
    };"""
    basic_route = """struct Route {
        char name[64] = {};
        uint16_t scope = 65535, outputs = 0, failed_outputs = 0;
        CheckedArray<Input> inputs;
        CheckedArray<Step> steps;
        bool enabled = true, failed = false;
        uint64_t next = 0;
        bool started = false, foreign = false;
        bool note_admission_closed = false, activated = false;
        uint64_t preparation_epoch = 0;
    };"""
    basic = (
        """#pragma once
#include "midi_bridge.h"
#include "checked_array.h"
#include "checked_list.h"
#include <algorithm>
#include <cstdint>

namespace cntrl::midi {
"""
        + common
        + """
class BasicProcessing {
public:
    static constexpr bool advanced = false;
"""
        + basic_step
        + basic_route
        + """
    template<class Core> struct State {};
};
} // namespace cntrl::midi
"""
    )
    (directory / 'midi_routing_types.h').write_text(basic)
    # Public nested names remain source compatible for existing clients.
    aliases = '\n'.join(
        f'    using {n} = cntrl::midi::{n};'
        for n in (
            'Emit',
            'Processor',
            'Field',
            'Assignment',
            'Input',
            'Work',
            'ParameterBinding',
            'ParameterDelivery',
            'Capture',
            'Visit',
        )
    )
    source = source.replace(
        '    using Factory =',
        aliases + '\n    using Step = typename ProcessingPolicy::Step;\n'
        '    using Route = typename ProcessingPolicy::Route;\n    using Factory =',
        1,
    )
    source = source.replace(
        'class Streams :', 'template<class ProcessingPolicy> class Routing :'
    )
    source = re.sub(r'\bStreams\b', 'Routing', source)
    source = (
        source.replace('#include "midi_file.h"\n', '')
        .replace('#include "midi_stream_timeline.h"\n', '')
        .replace('#include "midi_lfo.h"\n', '')
        .replace('#include "numeric.h"\n', '')
        .replace('#include <cmath>\n', '')
    )
    source = source.replace(
        '#include "midi_bridge.h"', '#include "midi_routing_types.h"'
    )
    source = source.replace('public:\n', '    friend ProcessingPolicy;\npublic:\n', 1)

    # State is owned by the selected policy, not by every routing instance.
    state_lines = []
    state_names = (
        'parameters_',
        'bindings_',
        'binding_targets_',
        'binding_owners_',
        'downstream_reached_',
        'beat_',
        'parameter_cursor_',
        'captures_',
        'reverse_parameters_',
        'visits_',
        'release_visit_cursor_',
        'release_capture_cursor_',
        'pressure_capture_cursor_',
        'file_provider_',
    )
    for name in state_names:
        match = re.search(
            r'^    (?:mutable )?(?:Checked\w+<[^\n]+>(?:::iterator)?|'
            r'BeatPosition|StreamFileProvider)\s*\*?'
            + name
            + r'(?:\s*=\s*nullptr)?;\n',
            source,
            re.MULTILINE,
        )
        if not match:
            raise ValueError(name)
        state_lines.append(match[0])
        source = source[: match.start()] + source[match.end() :]
    state = ''.join(state_lines)
    source = source.replace(
        '    Limits limits_;',
        '    typename ProcessingPolicy::template State<Routing> extension_;\n'
        '    Limits limits_;',
    )
    # Policy code can access the same queue via its friend relationship. No
    # second routing or ownership implementation is introduced.
    advanced_functions = {}
    # Remaining signatures are read from the original declaration to keep this
    # one-time migration tied to the concrete implementation being extracted.
    names = (
        'lfo_status',
        'parameter_write',
        'clock_outputs',
        'parameter_step',
        'parameter_bindings',
        'validate_lfos',
        'bind_lfo',
        'service_lfos',
        'stop_parameter',
        'accept_parameter',
        'service_parameters',
        'generated',
        'notify_release',
        'reverse_group',
        'finish_reverse',
        'flush_reverse',
        'numeric_fields',
    )
    for name in names:
        match = re.search(
            r'^    (?:static )?[\w:&<> ]+\b'
            + name
            + r'\([^\n]*\)(?: const| override)? \{',
            source,
            re.MULTILINE,
        )
        if not match:
            raise ValueError('method ' + name)
        start, end = block(source, match[0])
        definition = source[start:end]
        signature, body = definition.split('{', 1)
        signature = signature.strip()
        params = signature[signature.index('(') + 1 : signature.rindex(')')]
        arguments = []
        for parameter in params.split(','):
            if parameter.strip():
                arguments.append(re.findall(r'\b\w+\b', parameter.split('=')[0])[-1])
        result_type = signature[: signature.index(name)].replace('static ', '').strip()
        const = ' const' if signature.endswith(' const') else ''
        static = signature.startswith('static ')
        call = (
            f'ProcessingPolicy::template {name}<Routing>({", ".join(arguments)})'
            if static
            else f'ProcessingPolicy::{name}(*this'
            f'{", " if arguments else ""}{", ".join(arguments)})'
        )
        fallback = (
            'return true;'
            if result_type == 'bool'
            else 'return {};'
            if result_type != 'void'
            else ''
        )
        if name == 'numeric_fields':
            fallback = 'return basic_numeric_fields(step, work);'
        wrapper = (
            signature
            + ' {\n        if constexpr (ProcessingPolicy::advanced) '
            + ('return ' if result_type != 'void' else '')
            + call
            + ';\n        else { '
            + fallback
            + ' }\n    }'
        )
        source = source[:start] + '    ' + wrapper + source[end:]
        advanced_functions[name] = (signature, body)
    # Core member names are qualified only in extracted function bodies. This
    # leaves local variables and references through processor adapters intact.
    core_names = set(re.findall(r'\b\w+_\b', source))
    core_methods = set(
        re.findall(
            r'^    (?! )(?:template[^\n]+? )?(?:static )?[\w:&<> ]+ (\w+)\(',
            source,
            re.MULTILINE,
        )
    )
    core_methods.update(names)
    types = (
        'Step',
        'Route',
        'LfoStatus',
        'Work',
        'Event',
        'Processing',
        'Preparation',
        'Reader',
        'Voice',
        'Hold',
        'Capture',
        'Visit',
        'ParameterBinding',
        'ParameterDelivery',
    )

    def qualify(body: str) -> str:
        body = body.replace('*this', 'core')
        for name in sorted(core_names | core_methods, key=len, reverse=True):
            body = re.sub(r'(?<![.>:\w])\b' + name + r'\b', 'core.' + name, body)
        for name in state_names:
            body = body.replace('core.' + name, 'core.extension_.' + name)
        return body

    methods = []
    for name, (signature, body) in advanced_functions.items():
        static = signature.startswith('static ')
        result_type = signature[: signature.index(name)].replace('static ', '').strip()
        params = signature[signature.index('(') + 1 : signature.rindex(')')]
        const = signature.endswith(' const')
        aliases_inside = '\n' + '\n'.join(
            f'        using {n} = typename Core::{n};'
            for n in types
            if re.search(r'\b' + n + r'\b', body)
        )
        aliases_inside += (
            '\n'
            + '\n'.join(
                f'        constexpr auto {n} = Core::{n};'
                for n in ('max_routes', 'system_scope')
                if re.search(r'\b' + n + r'\b', body)
            )
            + '\n'
        )
        for type_name in types:
            params = re.sub(
                r'\b' + type_name + r'\b', 'typename Core::' + type_name, params
            )
        if result_type in types:
            result_type = 'typename Core::' + result_type
        if static:
            # Static transformations use only the core's shared value helpers.
            adapted = body
            for method in core_methods:
                adapted = re.sub(
                    r'(?<![.>:\w])\b' + method + r'(?=\()', 'Core::' + method, adapted
                )
            head = f'    template<class Core> static {result_type} {name}({params}) {{'
        else:
            adapted = qualify(body)
            head = (
                f'    template<class Core> static {result_type} {name}('
                f'{"const " if const else ""}Core &core'
                f'{", " if params else ""}{params}) {{'
            )
        methods.append(head + aliases_inside + adapted)

    # Adapters are instantiated only by advanced methods, after Core is complete.
    adapters = []
    # The original adapters are extracted separately below because their base
    # classes occur between the name and opening brace.
    for name in ('GeneratedSink', 'ProcessorOutput'):
        match = re.search(r'^    struct ' + name + r'[^\n]*\{', source, re.MULTILINE)
        if match is None:
            raise ValueError(name)
        start, end = block(source, match[0])
        adapter = source[start:end].replace('Routing', 'Core')
        adapter = adapter.replace(
            '    struct ' + name, '    template<class Core> struct ' + name
        )
        adapter = adapter.replace(
            '{\n',
            '{\n        using Work = typename Core::Work;\n'
            '        static constexpr unsigned max_routes = Core::max_routes;\n',
            1,
        )
        adapter = re.sub(r'(?<![.>:\w])\bis_hold\(', 'Core::is_hold(', adapter)
        adapters.append(adapter)
        source = source[:start] + source[end:]
    # State's parameter-independent records are defined in the shared header.
    advanced = (
        """#pragma once
#include "midi_routing_types.h"
#include "midi_file.h"
#include "midi_stream_timeline.h"
#include "midi_lfo.h"
#include "numeric.h"
#include <cmath>
namespace cntrl::midi {
class AdvancedProcessing {
public:
    static constexpr bool advanced = true;
"""
        + step
        + route.replace('system_scope', '65535')
        + '\n    template<class Core> struct State {\n'
        + '        using Work = typename Core::Work;\n'
        + state
        + '    };\n'
        + '\n'.join(adapters + methods)
        + '\n};\n} // namespace cntrl::midi\n#include "midi_routing.h"\n'
        'namespace cntrl::midi { using Streams = Routing<AdvancedProcessing>; }\n'
    )
    # References in shared lifecycle are made dependent. Follow-up edits add
    # guards at lifecycle boundaries rather than retaining advanced state.
    for name in state_names:
        source = re.sub(r'(?<![.>:\w])\b' + name + r'\b', 'extension_.' + name, source)
        source = source.replace('candidate.' + name, 'candidate.extension_.' + name)
    source = source.replace(
        '};\n} // namespace cntrl::midi',
        '};\nusing BasicRouting = Routing<BasicProcessing>;\n'
        '} // namespace cntrl::midi',
    )
    source = source.replace(
        'sizeof(Route) == 184', '(!ProcessingPolicy::advanced || sizeof(Route) == 184)'
    )
    (directory / 'midi_routing.h').write_text(source)
    (directory / 'midi_processing.h').write_text(advanced)


def finish(directory: Path, original: str) -> None:
    target = directory / 'midi_processing.h'
    advanced = target.read_text()
    if 'static bool modulation(' in advanced:
        raise SystemExit(
            'Advanced hooks already exist; refusing to overwrite maintained code.'
        )
    members = set(re.findall(r'\b\w+_\b', original))
    methods = set(
        re.findall(
            r'^    (?! )(?:template[^\n]+? )?(?:static )?[\w:&<> ]+ (\w+)\(',
            original,
            re.MULTILINE,
        )
    )
    state = {
        'parameters_',
        'bindings_',
        'binding_targets_',
        'binding_owners_',
        'downstream_reached_',
        'beat_',
        'parameter_cursor_',
        'captures_',
        'reverse_parameters_',
        'visits_',
        'release_visit_cursor_',
        'release_capture_cursor_',
        'pressure_capture_cursor_',
        'file_provider_',
    }
    types = (
        'Step',
        'Route',
        'Work',
        'Event',
        'Processing',
        'Preparation',
        'Reader',
        'Voice',
        'Hold',
        'Capture',
        'Visit',
        'ParameterBinding',
        'ParameterDelivery',
    )

    def qualify(body: str) -> str:
        body = body.replace('*this', 'core')
        for name in sorted(members, key=len, reverse=True):
            body = re.sub(
                r'(?<![.>:\w])\b' + name + r'\b',
                'core.' + ('extension_.' if name in state else '') + name,
                body,
            )
        for name in sorted(methods, key=len, reverse=True):
            body = re.sub(r'(?<![.>:\w])\b' + name + r'(?=\()', 'core.' + name, body)
        body = body.replace(
            'GeneratedSink output', 'GeneratedSink<Core> output'
        ).replace('ProcessorOutput output', 'ProcessorOutput<Core> output')
        return body

    def between(begin: str, end: str) -> str:
        start = original.index(begin)
        return original[start : original.index(end, start)]

    modulation = (
        between('        bool modulation = false;', '        unsigned work_budget')
        + '        return modulation;\n'
    )
    generators = between(
        '        for (unsigned i = 0; i < routes_.size(); ++i) '
        'if (active(i) && !routes_[i].steps.empty() && routes_[i].steps[0].kind == 8)',
        '        service_event_preparation(budget);',
    )
    parameters = between(
        '        for (unsigned i = 0; i < routes_.size(); ++i) '
        'if (routes_[i].lfo && routes_[i].lfo->ended',
        '        reclaim_owner_generation();',
    )
    process = (
        '        const unsigned route = next.route, position = next.position;\n'
        '        Work work = next.work;\n        auto &entry = core.routes_[route];\n'
        + between(
            '        if (next.phase == Preparation::echo) {',
            '        if (next.phase == Preparation::steps) { '
            'prepare({route, position, work, Preparation::outputs}); return; }',
        )
    )
    decode = (
        between(
            '                if (step.kind >= 128) {',
            '                if (!payload.ok || payload.cursor != payload.end) '
            'return false;',
        )
        + '        return true;\n'
    )
    hooks = []
    for signature, body in (
        ('bool modulation(const Core &core)', modulation),
        (
            'void service_generators(Core &core, uint64_t now, '
            'unsigned &used, unsigned work_budget)',
            generators,
        ),
        ('void finish_parameters(Core &core)', parameters),
        ('void process_step(Core &core, typename Core::Processing next)', process),
        (
            'bool decode_step(Core &core, typename Core::Route &route, '
            'typename Core::Step &step, typename Core::Reader &payload, '
            'unsigned length, unsigned reserved, unsigned j, unsigned inputs)',
            decode,
        ),
    ):
        aliases = '\n'.join(
            f'        using {n} = typename Core::{n};'
            for n in types
            if re.search(r'\b' + n + r'\b', body)
        )
        aliases += '\n' + '\n'.join(
            f'        constexpr auto {n} = Core::{n};'
            for n in ('max_routes', 'system_scope')
            if re.search(r'\b' + n + r'\b', body)
        )
        hooks.append(
            '    template<class Core> static '
            + signature
            + ' {\n'
            + aliases
            + '\n'
            + qualify(body)
            + '    }\n'
        )
    marker = '};\n} // namespace cntrl::midi'
    advanced = advanced.replace(marker, '\n' + '\n'.join(hooks) + marker, 1)
    target.write_text(advanced)


if __name__ == '__main__':
    tyro.cli(Split).run()
