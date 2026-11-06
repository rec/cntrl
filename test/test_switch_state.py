"""Shared MIDI state and selection hooks use the normal compiled switch path."""

import subprocess
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


def test_shared_state_and_selection_hooks(tmp_path: Path) -> None:
    root = Path(__file__).parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )

    def cc(control: int, value: int) -> dict[str, object]:
        return {'output': 'din', 'channel': 1, 'controller': control, 'value': value}

    show = {
        'patches': {'loud': cc(7, 127)},
        'switches': {
            'first': {
                'button': 'A',
                'mode': 'latched',
                'on': {'patch': 'loud'},
                'off': cc(7, 0),
                'enter': {'action': cc(20, 1)},
                'exit': cc(20, 0),
            },
            'alias': {
                'button': 'B',
                'mode': 'latched',
                'on': {'patch': 'loud'},
                'off': cc(7, 0),
                'enter': {'action': cc(21, 1), 'override': True},
                'exit': cc(21, 0),
            },
            'program': {
                'button': 'C',
                'on': {'output': 'din', 'channel': 1, 'program': 5},
            },
            'compound': {
                'button': 'D',
                'on': {'patches': [{'patch': 'loud'}, cc(10, 64)]},
            },
            'derived': {
                'button': 'E',
                'mode': 'latched',
                'on': {'parent': 'loud', 'value': 32},
                'off': cc(7, 0),
                'enter': {'action': cc(22, 1)},
                'exit': cc(22, 0),
            },
        },
    }
    data = compiler.compile_show(show, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data
    fixture = tmp_path / 'state.bin'
    fixture.write_bytes(data)
    momentary = {
        'switches': {
            'held': {
                'button': 'A',
                'mode': 'momentary',
                'on': cc(7, 127),
                'off': cc(7, 0),
                'enter': {'action': cc(20, 1)},
                'exit': cc(20, 0),
            },
            'other': {'button': 'B', 'on': cc(8, 1)},
        }
    }
    held_fixture = tmp_path / 'held.bin'
    held_fixture.write_bytes(compiler.compile_show(momentary, {}, [], target))
    executable = tmp_path / 'state'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_switch_state.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable), str(fixture), str(held_fixture)], check=True)


@pytest.mark.parametrize(
    'hook',
    [
        {'enter': {'action': {'controller': 7, 'value': 1}, 'override': 'yes'}},
        {'enter': {'action': {'patch': 'missing'}}},
        {'enter': {'action': {'operation': 'menu'}}},
        {'exit': {'operation': 'back'}},
    ],
)
def test_invalid_selection_hooks_are_rejected(hook: dict[str, object]) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    show = {
        'switches': {
            'test': {'button': 'A', 'on': {'operation': 'brightness', 'value': 3}}
            | hook
        }
    }
    with pytest.raises(ValueError):
        compiler.compile_show(show, {}, [], target)
