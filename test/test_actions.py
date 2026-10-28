"""Assignment validation and authoritative setup round trips."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


def compile_actions(assignments: dict[str, object], capacity: int = 5) -> bytes:
    root = Path(__file__).parents[1] / 'firmware/test'
    show = compiler.read_document(root / 'show.json')
    assert isinstance(show, dict)
    show['actions'] = assignments
    target = models.Target.model_validate(compiler.read_document(root / 'target.json'))
    target = target.model_copy(update={'max_actions': capacity})
    return compiler.compile_show(
        show, compiler.read_document(root / 'definitions.json'), [], target
    )


def assignment(mode: str = 'momentary') -> dict[str, object]:
    return {
        'button': 'A',
        'mode': mode,
        'on': {'instrument': 'a', 'controller': 64, 'value': 127},
        'off': {'instrument': 'a', 'controller': 64, 'value': 0},
    }


def test_actions_round_trip_and_native_fixture() -> None:
    sustain = assignment()
    solo = assignment('latched')
    solo['button'] = 'B'
    solo['on'] = {'instrument': 'a', 'controller': 7, 'value': 110}
    solo['off'] = {'instrument': 'a', 'controller': 7, 'value': 85}
    actions = {
        'sustain': sustain,
        'solo': solo,
        'alternate': {
            'button': 'C',
            'mode': 'momentary',
            'on': {'patch': 'Strings'},
            'off': {'patch': 'Pad'},
        },
    }
    data = compile_actions(actions)
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
        ('on', {'instrument': 'missing', 'controller': 64, 'value': 127}),
        ('off', {'instrument': 'a', 'controller': 64, 'value': True}),
        ('off', {'patch': 'Sax'}),
    ],
)
def test_invalid_assignment_is_rejected(field: str, value: object) -> None:
    action = assignment()
    action[field] = value
    with pytest.raises(ValueError):
        compile_actions({'sustain': action})


def test_duplicate_output_ownership_and_unsupported_build_are_rejected() -> None:
    second = assignment('latched')
    second['button'] = 'B'
    with pytest.raises(ValueError, match='ownership'):
        compile_actions({'first': assignment(), 'second': second})
    with pytest.raises(ValueError, match='capacity'):
        compile_actions({'sustain': assignment()}, 0)


def test_patch_reference_requires_unique_name() -> None:
    action = {
        'button': 'C',
        'mode': 'momentary',
        'on': {'patch': 'Sax'},
        'off': {'patch': 'Flute'},
    }
    assert compile_actions({'alternate': action})
    action['off'] = {'patch': 'absent'}
    with pytest.raises(ValueError, match='exactly one'):
        compile_actions({'alternate': action})
