"""A shared shift layer preserves action pairing and ordinary switch cleanup."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved
from scripts import native_tests


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )


def test_shifted_actions_round_trip_and_execute(
    tmp_path: Path, target: models.Target
) -> None:
    root = Path(__file__).parents[1]
    source = {
        'patches': {'higher': {'controller': 21, 'value': 100}},
        'instruments': {'synth': {'output': 'din', 'channel': 1}},
        'switches': {
            'shift': {
                'button': 'A',
                'mode': 'momentary',
                'on': {'operation': 'shift'},
                'off': {'operation': 'unshift'},
            },
            'held': {
                'button': 'B',
                'mode': 'momentary',
                'on': {'controller': 10, 'value': 127},
                'off': {'controller': 10, 'value': 0},
                'shifted': {
                    'on': {'controller': 11, 'value': 100},
                    'off': {'controller': 11, 'value': 0},
                },
            },
            'latched': {
                'button': 'C',
                'mode': 'latched',
                'on': {'controller': 20, 'value': 127},
                'off': {'controller': 20, 'value': 0},
                'shifted': {'on': 'higher', 'off': {'controller': 21, 'value': 0}},
            },
            'gestures': {
                'button': 'D',
                'on': {'controller': 30, 'value': 1},
                'gestures': {
                    'release': {'controller': 31, 'value': 9},
                    'debounce': 0.001,
                },
                'shifted': {
                    'on': {'controller': 30, 'value': 2},
                    'gestures': {
                        'long_press': {'controller': 30, 'value': 3},
                        'long_release': {'controller': 30, 'value': 4},
                        'debounce': 0.001,
                        'long_delay': 0.030,
                    },
                },
            },
            'indirect': {
                'button': 'E',
                'enter': {'action': {'operation': 'shift'}},
                'on': {
                    'patches': [
                        {'operation': 'shift'},
                        {'operation': 'shift'},
                        {'operation': 'unshift'},
                        {'operation': 'unshift'},
                    ]
                },
                'exit': {'operation': 'unshift'},
            },
        },
    }
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['switches']['shift']['on']['operation'] == 'shift'
    assert exported['switches']['held']['shifted']['on']['controller'] == 11
    assert compiler.compile_show(exported, {}, [], target) == data
    image = tmp_path / 'shifted.bin'
    image.write_bytes(data)
    executable = tmp_path / 'shifted'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_shifted_actions.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    native_tests.run_native([str(executable), str(image)], check=True)


@pytest.mark.parametrize(
    'assignment',
    [
        {'shifted': {'off': {'operation': 'unshift'}}},
        {'shifted': {'on': {'operation': 'shift'}, 'off': {'operation': 'unshift'}}},
        {
            'mode': 'momentary',
            'off': {'operation': 'unshift'},
            'shifted': {'on': {'operation': 'shift'}},
        },
        {'shifted': {'on': {'operation': 'menu'}}},
        {'shifted': {'on': 'missing'}},
    ],
)
def test_invalid_shifted_actions_are_rejected(
    assignment: dict[str, object], target: models.Target
) -> None:
    source = {
        'switches': {'test': {'button': 'A', 'on': {'operation': 'shift'}} | assignment}
    }
    with pytest.raises(compiler.CompilationError):
        compiler.compile_show(source, {}, [], target)


def test_shift_requires_switch_support_and_a_switch_context(
    target: models.Target,
) -> None:
    source = {'patch_cycles': {'test': [{'operation': 'shift'}]}}
    with pytest.raises(compiler.CompilationError, match='switch assignment'):
        compiler.compile_show(source, {}, [], target)
    with pytest.raises(compiler.CompilationError, match='target switch support'):
        compiler.compile_show(
            {'patches': {'shift': {'operation': 'shift'}}},
            {},
            [],
            target.model_copy(update={'max_switches': 0}),
        )
