"""Switch assignments invoke the same Patch graph as the browser."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved
from scripts import regenerate_fixtures


def test_switches_round_trip_and_native_fixture() -> None:
    data = regenerate_fixtures.compile_switches(
        regenerate_fixtures.fixture_assignments()
    )
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
        ('off', {'patch': 'absent'}),
    ],
)
def test_invalid_assignment_is_rejected(field: str, value: object) -> None:
    value_assignment = regenerate_fixtures.assignment()
    value_assignment[field] = value
    with pytest.raises(ValueError):
        regenerate_fixtures.compile_switches({'sustain': value_assignment})


def test_shared_controls_compile_and_unsupported_build_is_rejected() -> None:
    second = regenerate_fixtures.assignment('latched')
    second['button'] = 'B'
    assert regenerate_fixtures.compile_switches(
        {'first': regenerate_fixtures.assignment(), 'second': second}
    )
    with pytest.raises(ValueError, match='capacity'):
        regenerate_fixtures.compile_switches(
            {'sustain': regenerate_fixtures.assignment()}, 0
        )


def test_patch_references_use_stable_keys() -> None:
    value = {
        'button': 'C',
        'mode': 'momentary',
        'on': {'patch': 'Sax'},
        'off': {'patch': 'Flute'},
    }
    assert regenerate_fixtures.compile_switches({'alternate': value})
    value['off'] = {'patch': 'absent'}
    with pytest.raises(ValueError, match='does not exist'):
        regenerate_fixtures.compile_switches({'alternate': value})


def test_stock_reference_is_rejected_inside_persistent_sequence() -> None:
    root = Path(__file__).parents[1] / 'firmware/test'
    show = {
        'patches': {'stock': {'operation': 'stock-firmware', 'exit_buttons': 'A+F'}},
        'switches': {
            'alternate': {
                'button': 'C',
                'mode': 'momentary',
                'on': {
                    'patches': [{'patch': 'stock'}],
                },
                'off': {'operation': 'brightness', 'value': 192},
            }
        },
    }
    target = models.Target.model_validate(
        compiler.read_document(root / 'target.json')
    ).model_copy(update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')})
    with pytest.raises(ValueError, match='only press assignments'):
        compiler.compile_show(show, {}, [], target)
