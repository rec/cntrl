"""Switch assignments invoke the same Patch graph as the browser."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


def compile_switches(assignments: dict[str, object], capacity: int = 5) -> bytes:
    root = Path(__file__).parents[1] / 'firmware/test'
    show = compiler.read_document(root / 'show.json')
    assert isinstance(show, dict)
    groups = show['groups']
    assert isinstance(groups, dict)
    show['patches'] = {p['name']: p for v in groups.values() for p in v}
    show['switches'] = assignments
    target = models.Target.model_validate(
        compiler.read_document(root / 'target.json')
    ).model_copy(update={'max_switches': capacity})
    return compiler.compile_show(
        show, compiler.read_document(root / 'definitions.json'), [], target
    )


def assignment(mode: str = 'momentary') -> dict[str, object]:
    return {
        'button': 'A',
        'mode': mode,
        'on': {
            'type': 'control-change',
            'output': 'din',
            'channel': 1,
            'controller': 64,
            'value': 127,
        },
        'off': {
            'type': 'control-change',
            'output': 'din',
            'channel': 1,
            'controller': 64,
            'value': 0,
        },
    }


def fixture_assignments() -> dict[str, object]:
    solo = assignment('latched')
    solo['button'] = 'B'
    solo['on'] = {
        'type': 'control-change',
        'output': 'din',
        'channel': 1,
        'controller': 7,
        'value': 110,
    }
    solo['off'] = {
        'type': 'control-change',
        'output': 'din',
        'channel': 1,
        'controller': 7,
        'value': 85,
    }
    return {
        'sustain': assignment(),
        'solo': solo,
        'alternate': {
            'button': 'C',
            'mode': 'momentary',
            'on': {'type': 'reference', 'patch': 'Strings'},
            'off': {'type': 'reference', 'patch': 'Pad'},
        },
    }


def test_switches_round_trip_and_native_fixture() -> None:
    data = compile_switches(fixture_assignments())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    root = Path(__file__).parents[1] / 'firmware/test'
    target = models.Target.model_validate(compiler.read_document(root / 'target.json'))
    assert compiler.compile_show(exported, {}, [], target) == data
    assert data.hex() == (root / 'actions.golden').read_text().strip()


@pytest.mark.parametrize(
    'field,value',
    [
        ('button', 'F'),
        ('mode', 'toggle'),
        ('name', ''),
        (
            'on',
            {
                'type': 'control-change',
                'output': 'missing',
                'channel': 1,
                'controller': 64,
                'value': 127,
            },
        ),
        (
            'off',
            {
                'type': 'control-change',
                'output': 'din',
                'channel': 1,
                'controller': 64,
                'value': True,
            },
        ),
        ('off', {'type': 'reference', 'patch': 'absent'}),
    ],
)
def test_invalid_assignment_is_rejected(field: str, value: object) -> None:
    value_assignment = assignment()
    value_assignment[field] = value
    with pytest.raises(ValueError):
        compile_switches({'sustain': value_assignment})


def test_duplicate_output_ownership_and_unsupported_build_are_rejected() -> None:
    second = assignment('latched')
    second['button'] = 'B'
    with pytest.raises(ValueError, match='ownership'):
        compile_switches({'first': assignment(), 'second': second})
    with pytest.raises(ValueError, match='capacity'):
        compile_switches({'sustain': assignment()}, 0)


def test_patch_references_use_stable_keys() -> None:
    value = {
        'button': 'C',
        'mode': 'momentary',
        'on': {'type': 'reference', 'patch': 'Sax'},
        'off': {'type': 'reference', 'patch': 'Flute'},
    }
    assert compile_switches({'alternate': value})
    value['off'] = {'type': 'reference', 'patch': 'absent'}
    with pytest.raises(ValueError, match='does not exist'):
        compile_switches({'alternate': value})


def test_stock_reference_is_rejected_inside_persistent_sequence() -> None:
    root = Path(__file__).parents[1] / 'firmware/test'
    show = {
        'patches': {'stock': {'type': 'stock-firmware', 'exit_buttons': 'A+F'}},
        'switches': {
            'alternate': {
                'button': 'C',
                'mode': 'momentary',
                'on': {
                    'type': 'sequence',
                    'patches': [{'type': 'reference', 'patch': 'stock'}],
                },
                'off': {'type': 'state', 'operation': 'brightness', 'value': 192},
            }
        },
    }
    target = models.Target.model_validate(
        compiler.read_document(root / 'target.json')
    ).model_copy(update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')})
    with pytest.raises(ValueError, match='only press assignments'):
        compiler.compile_show(show, {}, [], target)
