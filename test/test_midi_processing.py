"""Portable route authoring and graph compilation contracts."""

import struct

import pytest
from pydantic import BaseModel, Field, ValidationError

from cntrl.patch_switcher import midi_processing
from cntrl.patch_switcher.models import MidiEndpoint


def endpoints() -> list[MidiEndpoint]:
    return [
        MidiEndpoint(name='din', label='DIN', route=0, input=True),
        MidiEndpoint(
            name='usb-host/keys',
            label='Keys',
            route=1,
            input=True,
            output=False,
            transport='usb-host',
        ),
        MidiEndpoint(
            name='usb-host/synth', label='Synth', route=2, transport='usb-host'
        ),
    ]


def test_thru_compiles_direct_endpoint_indexes() -> None:
    data = midi_processing.compile_routes(
        {'thru': {'from': 'din', 'to': 'usb-host/synth'}}, {}, endpoints()
    )
    assert midi_processing.HEADER.unpack_from(data) == (b'CSTR', len(data), 1, 1)
    scope, count, outputs, steps, name = midi_processing.ROUTE.unpack_from(
        data, midi_processing.HEADER.size
    )
    assert (scope, count, outputs, steps, name.rstrip(b'\0')) == (
        65535,
        1,
        4,
        0,
        b'thru',
    )
    assert midi_processing.INPUT.unpack_from(
        data, midi_processing.HEADER.size + midi_processing.ROUTE.size
    ) == (0, 0)


def test_no_routes_preserves_legacy_forwarding_but_empty_matrix_is_explicit() -> None:
    assert midi_processing.compile_routes(None, {}, endpoints()) == b''
    data = midi_processing.compile_routes({}, {}, endpoints())
    assert midi_processing.HEADER.unpack(data) == (
        b'CSTR',
        midi_processing.HEADER.size,
        0,
        1,
    )


def test_forward_references_and_patch_subscribers_share_system_route() -> None:
    data = midi_processing.compile_routes(
        {
            'synth': {'from': 'keys', 'to': 'usb-host/synth'},
            'keys': {'from': ['din', 'usb-host/keys']},
        },
        {
            7: {
                'harmony': {
                    'from': 'system/keys',
                    'via': [{'transpose': 7}],
                    'to': 'usb-host/synth',
                }
            }
        },
        endpoints(),
    )
    cursor = midi_processing.HEADER.size
    names: list[str] = []
    sources: list[list[tuple[int, int]]] = []
    scopes: list[int] = []
    for _ in range(3):
        scope, count, _, steps, name = midi_processing.ROUTE.unpack_from(data, cursor)
        cursor += midi_processing.ROUTE.size
        names.append(name.rstrip(b'\0').decode())
        scopes.append(scope)
        sources.append(
            [
                midi_processing.INPUT.unpack_from(
                    data, cursor + i * midi_processing.INPUT.size
                )
                for i in range(count)
            ]
        )
        cursor += count * midi_processing.INPUT.size
        for _ in range(steps):
            _, _, size = midi_processing.STEP.unpack_from(data, cursor)
            cursor += midi_processing.STEP.size + size
    assert names == ['keys', 'synth', 'harmony']
    assert sources == [[(0, 0), (0, 1)], [(1, 0)], [(1, 0)]]
    assert scopes == [65535, 65535, 7]
    assert cursor == len(data)


@pytest.mark.parametrize(
    'routes, message',
    [
        ({'din': {'from': 'din', 'to': 'usb-host/synth'}}, 'reserved'),
        ({'bad/name': {'from': 'din', 'to': 'usb-host/synth'}}, 'reserved'),
        ({'keys': {'from': 'unknown', 'to': 'usb-host/synth'}}, 'unknown'),
        ({'keys': {'from': 'usb-host/synth', 'to': 'din'}}, 'not an input'),
        ({'keys': {'from': 'din', 'to': 'usb-host/keys'}}, 'invalid output'),
        ({'keys': {'from': 'other'}, 'other': {'from': 'keys', 'to': 'din'}}, 'cycle'),
        ({'keys': {'from': 'din'}}, 'unused'),
        ({'keys': {'from': 'system/keys', 'to': 'din'}}, 'only valid inside patches'),
    ],
)
def test_invalid_graph_is_rejected_before_delivery(
    routes: dict[str, object], message: str
) -> None:
    with pytest.raises(ValueError, match=message):
        midi_processing.compile_routes(routes, {}, endpoints())


@pytest.mark.parametrize(
    'route',
    [
        {'from': [], 'to': 'din'},
        {'from': ['din', 'din'], 'to': 'din'},
        {'from': 'din', 'generate': {'clock': {}}, 'to': 'din'},
        {'to': 'din'},
        {'from': 'din', 'via': [{'transpose': True}], 'to': 'din'},
        {'from': 'din', 'via': [{'transpose': 128}], 'to': 'din'},
        {'from': 'din', 'via': [{'transpose': 7, 'invert': 60}], 'to': 'din'},
        {'from': 'din', 'via': [{'keep': {'channel': 17}}], 'to': 'din'},
        {'from': 'din', 'via': [{'keep': {'type': 'midi'}}], 'to': 'din'},
        {
            'from': 'din',
            'via': [{'delay': {'time': '2 ms', 'repeats': 0}}],
            'to': 'din',
        },
        {
            'from': 'din',
            'via': [
                {'scale': {'field': 'velocity', 'input': [2, 2], 'output': [0, 127]}}
            ],
            'to': 'din',
        },
        {'generate': {'unrecognized': {}}, 'to': 'din'},
    ],
)
def test_invalid_route_and_processor_arguments_are_rejected(
    route: dict[str, object],
) -> None:
    with pytest.raises(ValidationError):
        midi_processing.Route.model_validate(route)


def test_units_and_echo_defaults_compile_once() -> None:
    data = midi_processing.compile_routes(
        {'echo': {'from': 'din', 'to': 'usb-host/synth', 'via': [{'delay': '250 ms'}]}},
        {},
        endpoints(),
    )
    offset = (
        midi_processing.HEADER.size
        + midi_processing.ROUTE.size
        + midi_processing.INPUT.size
    )
    kind, reserved, size = midi_processing.STEP.unpack_from(data, offset)
    assert (kind, reserved, size) == (6, 0, 14)
    assert struct.unpack_from('<QHf', data, offset + midi_processing.STEP.size) == (
        250000,
        1,
        1,
    )


def test_scale_implicit_input_range_is_chosen_from_each_message() -> None:
    data = midi_processing.compile_routes(
        {
            'controls': {
                'from': 'din',
                'to': 'usb-host/synth',
                'via': [{'scale': {'field': 'value', 'output': [127, 0]}}],
            }
        },
        {},
        endpoints(),
    )
    offset = (
        midi_processing.HEADER.size
        + midi_processing.ROUTE.size
        + midi_processing.INPUT.size
        + midi_processing.STEP.size
    )
    assert data[offset] == 0
    assert struct.unpack_from('<Biiii', data, offset + 1) == (5, -1, -1, 127, 0)


@pytest.mark.parametrize(
    'source,destination',
    [
        ('control-change', 'note-on'),
        ('note-on', 'control-change'),
        ('note-off', 'program-change'),
    ],
)
def test_type_conversion_cannot_create_or_discard_note_pair(
    source: str, destination: str
) -> None:
    with pytest.raises(ValidationError, match='paired'):
        midi_processing.Map.model_validate(
            {
                'match': {'type': source},
                'set': {
                    'type': destination,
                    'channel': 1,
                    'note': 60,
                    'velocity': 100,
                    'controller': 11,
                    'value': 5,
                },
            }
        )


def test_program_to_controller_conversion_preserves_channel_and_copies_value() -> None:
    route = {
        'from': 'din',
        'to': 'usb-host/synth',
        'via': [
            {
                'map': {
                    'match': {'type': 'program-change', 'channel': 1},
                    'set': {
                        'type': 'control-change',
                        'controller': 11,
                        'value': {'from': 'program', 'offset': -1},
                    },
                }
            }
        ],
    }
    data = midi_processing.compile_routes({'convert': route}, {}, endpoints())
    assert data.endswith(struct.pack('<BBi', 5, 6, -1))
    assignments = route['via'][0]['map']['set']
    route['via'][0]['map']['set'] = dict(reversed(assignments.items()))
    assert midi_processing.compile_routes({'convert': route}, {}, endpoints()) == data


def test_reverse_requires_finite_clip_or_window() -> None:
    with pytest.raises(ValueError, match='requires a window'):
        midi_processing.compile_routes(
            {
                'reverse': {
                    'from': 'din',
                    'to': 'usb-host/synth',
                    'via': [{'reverse': {}}],
                }
            },
            {},
            endpoints(),
        )
    assert midi_processing.compile_routes(
        {
            'reverse': {
                'from': 'din',
                'to': 'usb-host/synth',
                'via': [{'reverse': {'window': '2 s'}}],
            }
        },
        {},
        endpoints(),
    )


def test_file_generator_supplies_finite_reverse_boundaries() -> None:
    assert midi_processing.compile_routes(
        {
            'reverse': {
                'generate': {'file': {'filename': 'song.mid'}},
                'to': 'usb-host/synth',
                'via': [{'reverse': {}}],
            }
        },
        {},
        endpoints(),
    )


def test_registered_processor_compiles_without_builtin_dispatch_changes(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class VelocityFloor(BaseModel, frozen=True):
        minimum: int = Field(ge=1, le=127)

    def encode(value: BaseModel) -> bytes:
        assert isinstance(value, VelocityFloor)
        return bytes([value.minimum])

    monkeypatch.setattr(midi_processing, 'REGISTERED_PROCESSORS', {})
    midi_processing.register_processor(
        'velocity-floor', 128, VelocityFloor, encode, ram_bytes=256, version=2
    )
    data = midi_processing.compile_routes(
        {
            'keys': {
                'from': 'din',
                'to': 'usb-host/synth',
                'via': [{'velocity-floor': {'minimum': 40}}],
            }
        },
        {},
        endpoints(),
    )
    assert data.endswith(midi_processing.STEP.pack(128, 2, 1) + bytes([40]))
    assert midi_processing.estimate_ram(data) == 4096 + 163840 + 4 * len(data) + 256
    with pytest.raises(ValidationError):
        midi_processing.compile_routes(
            {
                'keys': {
                    'from': 'din',
                    'to': 'usb-host/synth',
                    'via': [{'velocity-floor': {'minimum': 128}}],
                }
            },
            {},
            endpoints(),
        )
    with pytest.raises(ValueError, match='duplicate'):
        midi_processing.register_processor('another', 128, VelocityFloor, encode)


def test_clock_generators_have_one_owner_per_active_output() -> None:
    clock = {'generate': {'clock': {}}, 'to': 'din'}
    with pytest.raises(ValueError, match='multiple active clock'):
        midi_processing.compile_routes({'one': clock, 'two': clock}, {}, endpoints())
    with pytest.raises(ValueError, match='multiple active clock'):
        midi_processing.compile_routes({'one': clock}, {7: {'two': clock}}, endpoints())
    assert midi_processing.compile_routes(
        None, {7: {'one': clock}, 9: {'two': clock}}, endpoints()
    )
    with pytest.raises(ValueError, match='multiple active clock'):
        midi_processing.compile_routes(
            {'one': clock, 'duplicate': {'from': 'one', 'to': 'din'}}, {}, endpoints()
        )


def test_registered_generator_uses_no_input_and_its_config_defaults(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class ControllerGenerator(BaseModel, frozen=True):
        controller: int = Field(default=11, ge=0, le=127)

    def encode(value: BaseModel) -> bytes:
        assert isinstance(value, ControllerGenerator)
        return bytes([value.controller])

    monkeypatch.setattr(midi_processing, 'REGISTERED_PROCESSORS', {})
    midi_processing.register_processor(
        'modulation', 129, ControllerGenerator, encode, generator=True
    )
    data = midi_processing.compile_routes(
        {'expression': {'generate': {'modulation': {}}, 'to': 'din'}}, {}, endpoints()
    )
    scope, inputs, outputs, steps, _ = midi_processing.ROUTE.unpack_from(
        data, midi_processing.HEADER.size
    )
    assert (scope, inputs, outputs, steps) == (65535, 0, 1, 1)
    assert data.endswith(midi_processing.STEP.pack(129, 1, 1) + bytes([11]))
    with pytest.raises(
        ValidationError, match='a generator must be declared in generate'
    ):
        midi_processing.Route.model_validate(
            {'from': 'din', 'via': [{'modulation': {}}], 'to': 'din'}
        )
    with pytest.raises(ValidationError, match='exactly one of from and generate'):
        midi_processing.Route.model_validate(
            {'from': 'din', 'generate': {'modulation': {}}, 'to': 'din'}
        )
