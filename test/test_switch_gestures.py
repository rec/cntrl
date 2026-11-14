"""Gesture patches survive authoring, compilation and native execution."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved
from scripts import native_tests


@pytest.fixture(scope='session')
def gesture_runner(native_programs: native_tests.NativePrograms) -> Path:
    return native_programs.build('test/test_switch_gestures.cpp')


@pytest.mark.parametrize('fast_switches', [True, False])
@pytest.mark.parametrize('long_double', [True, False])
def test_gesture_patches_round_trip_and_execute(
    tmp_path: Path, gesture_runner: Path, fast_switches: bool, long_double: bool
) -> None:
    assert models.Setup().fast_switches is True
    root = Path(__file__).parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    patches = [
        dict(type='control-change', output='din', channel=1, controller=7, value=i)
        for i in range(9 if long_double else 7)
    ]
    events = (
        'release',
        'long_press',
        'long_release',
        'double_tap',
        'double_release',
        'repeat',
    ) + (('long_double_tap', 'long_double_release') if long_double else ())
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
    native_tests.run_native(
        [
            str(gesture_runner),
            str(fixture),
            str(int(fast_switches)),
            str(int(long_double)),
        ],
        check=True,
    )


@pytest.mark.parametrize('long_triple', [False, True])
def test_triple_actions_round_trip_and_execute(
    tmp_path: Path, gesture_runner: Path, long_triple: bool
) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'firmware/test/target.json')
    )
    patches = [
        dict(type='control-change', output='din', channel=1, controller=7, value=i)
        for i in range(13)
    ]
    source = {
        'switches': {
            'test': {
                'button': 'A',
                'on': patches[0],
                'gestures': {
                    e: patches[i + 1]
                    for i, e in enumerate(compiler.GESTURE_EVENTS)
                    if long_triple or not e.startswith('long_triple')
                },
            }
        }
    }
    data = compiler.compile_show(source, {}, [], target)
    assert (
        compiler.compile_show(
            tomlkit.parse(resolved.export(data)).unwrap(), {}, [], target
        )
        == data
    )
    fixture = tmp_path / 'triple.bin'
    fixture.write_bytes(data)
    native_tests.run_native([str(gesture_runner), str(fixture), 'triple'], check=True)


@pytest.mark.parametrize(
    'changes',
    [
        {'mode': 'latched', 'off': {'operation': 'brightness', 'value': 3}},
        {'gestures': {'long_release': {'operation': 'brightness', 'value': 3}}},
        {'gestures': {'double_release': {'operation': 'brightness', 'value': 3}}},
        {'gestures': {'long_double_release': {'operation': 'brightness', 'value': 3}}},
        {'gestures': {'long_double_tap': {'patch': 'missing'}}},
        {'gestures': {'triple_release': {'operation': 'brightness', 'value': 3}}},
        {'gestures': {'long_triple_release': {'operation': 'brightness', 'value': 3}}},
        {'gestures': {'triple_tap': {'patch': 'missing'}}},
        {'gestures': {'long_triple_tap': {'patch': 'missing'}}},
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
