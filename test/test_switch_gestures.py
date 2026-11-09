"""Gesture patches survive authoring, compilation and native execution."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved
from scripts import native_tests


@pytest.mark.parametrize('fast_switches', [True, False])
def test_gesture_patches_round_trip_and_execute(
    tmp_path: Path, fast_switches: bool
) -> None:
    assert models.Setup().fast_switches is True
    root = Path(__file__).parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    patches = [
        dict(type='control-change', output='din', channel=1, controller=7, value=i)
        for i in range(7)
    ]
    events = (
        'release',
        'long_press',
        'long_release',
        'double_tap',
        'double_release',
        'repeat',
    )
    show = {
        'fast_switches': fast_switches,
        'switches': {
            'test': {
                'button': 'A',
                'on': patches[0],
                'gestures': dict(zip(events, patches[1:], strict=True)),
            }
        },
    }
    data = compiler.compile_show(show, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['fast_switches'] is fast_switches
    assert compiler.compile_show(exported, {}, [], target) == data
    fixture = tmp_path / 'gestures.bin'
    fixture.write_bytes(data)
    executable = tmp_path / 'gestures'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_switch_gestures.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    native_tests.run_native(
        [str(executable), str(fixture), str(int(fast_switches))], check=True
    )


@pytest.mark.parametrize(
    'changes',
    [
        {'mode': 'latched', 'off': {'operation': 'brightness', 'value': 3}},
        {'gestures': {'long_release': {'operation': 'brightness', 'value': 3}}},
        {'gestures': {'double_release': {'operation': 'brightness', 'value': 3}}},
        {'gestures': {'debounce': '50 ms', 'double_delay': '10 ms'}},
        {'gestures': {'repeat_interval': 0}},
        {'gestures': {'repeat': {'patch': 'missing'}}},
    ],
)
def test_invalid_gesture_assignments_are_rejected(changes: dict[str, object]) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    assignment = {
        'button': 'A',
        'on': {'operation': 'brightness', 'value': 3},
        'gestures': {},
    } | changes
    with pytest.raises(ValueError):
        compiler.compile_show({'switches': {'test': assignment}}, {}, [], target)
