"""Generate firmware capability data from the host compiler's target model."""

import json

from ..patch_switcher.models import Target


def header(target: Target) -> str:
    scan_interval = round(target.scan_interval * 1000)
    maximum_area = max(d.width * d.height for d in target.displays)
    total_area = sum(d.width * d.height for d in target.displays)
    endpoint_count = len(target.midi_endpoints)
    cable_indices = [
        i
        for i, e in enumerate(target.midi_endpoints)
        if e.input and e.output and e.transport == 'physical'
    ]
    lines = [
        '#pragma once',
        '#include <array>',
        '#include <cntrl/midi_outputs.h>',
        '#include <cntrl/ui/display.h>',
        '#include <cntrl/ui/controls.h>',
        'namespace cntrl::target {',
        f'inline constexpr unsigned memory_capacity = {target.memory.capacity};',
        f'inline constexpr unsigned memory_runtime = {target.memory.runtime};',
        f'inline constexpr unsigned max_switches = {target.max_switches};',
        f'inline constexpr unsigned scan_interval_ms = {scan_interval};',
        f'inline constexpr unsigned display_count = {len(target.displays)};',
        f'inline constexpr unsigned button_count = {len(target.buttons)};',
        'inline constexpr std::array<ui::Viewport, display_count> viewports = {{',
        *[
            f'    {{{d.width}, {d.height}, {d.gap}, {d.corner_radius}}},'
            for d in target.displays
        ],
        '}};',
        'inline constexpr std::array<const char *, display_count> display_ids = {{',
        *[f'    {json.dumps(d.id)},' for d in target.displays],
        '}};',
        'inline constexpr std::array<const char *, display_count> display_labels = {{',
        *[f'    {json.dumps(d.label)},' for d in target.displays],
        '}};',
        'inline constexpr std::array<const char *, button_count> button_labels = {{',
        *[f'    {json.dumps(n)},' for n in target.buttons],
        '}};',
        f'inline constexpr unsigned maximum_display_area = {maximum_area};',
        f'inline constexpr unsigned total_display_area = {total_area};',
        'inline constexpr std::array<midi::OutputConnection, '
        f'{endpoint_count}> endpoints = {{{{',
    ]
    for endpoint in target.midi_endpoints:
        lines.append(
            '    {'
            + ', '.join(
                [
                    json.dumps(endpoint.name),
                    json.dumps(endpoint.label),
                    str(endpoint.route),
                    f'midi::OutputTransport::{endpoint.transport.replace("-", "_")}',
                    str(endpoint.input).lower(),
                    str(endpoint.output).lower(),
                    str(endpoint.port)
                    if endpoint.port is not None
                    else 'midi::no_port',
                ]
            )
            + '},'
        )
    lines.extend(
        [
            '}};',
            'inline constexpr midi::Outputs '
            f'midi_connections{{endpoints.data(), {endpoint_count}}};',
            'inline constexpr std::array<midi::OutputConnection, '
            f'{len(cable_indices)}> cable_endpoints = {{{{',
            *[f'    endpoints[{i}],' for i in cable_indices],
            '}};',
        ]
    )
    cable_count = len(cable_indices)
    port_count = max(
        (e.port + 1 for e in target.midi_endpoints if e.port is not None), default=0
    )
    lines.extend(
        [
            'inline constexpr midi::Outputs '
            f'cable_connections{{cable_endpoints.data(), {cable_count}}};',
            f'inline constexpr unsigned port_count = {port_count};',
            'inline constexpr std::array<ui::ControlBinding, button_count> '
            'control_bindings = {{',
        ]
    )
    for name in target.buttons:
        action = (
            str(APP_ACTIONS.index(target.controls.app_actions[name]))
            if name in target.controls.app_actions
            else 'ui::Controls::absent'
        )
        lines.append(f'    {{{action}, {json.dumps(name)}}},')
    lines.extend(['}};', 'inline constexpr ui::Controls controls{'])
    for name in CONTROL_ROLES:
        identity = getattr(target.controls, name)
        index = (
            str(target.buttons.index(identity))
            if identity is not None
            else 'ui::Controls::absent'
        )
        lines.append(f'    {index},')
    lines.extend(
        [
            '    control_bindings.data(), button_count};',
            '} // namespace cntrl::target',
            '',
        ]
    )
    return '\n'.join(lines)


APP_ACTIONS = (
    'previous-patch',
    'next-patch',
    'activate-selection',
    'previous-group',
    'next-group',
    'undo',
)
CONTROL_ROLES = (
    'previous',
    'next',
    'confirm',
    'previous_section',
    'next_section',
    'back',
    'next_page',
    'reverse_page',
    'tap',
)
