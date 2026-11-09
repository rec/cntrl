"""Authored waits execute through the shared nonblocking patch executor."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved
from scripts import native_tests


@pytest.mark.parametrize('duration', [0, -1, True, 65.536, '1 Hz', '0.1 ms'])
def test_invalid_delay_is_rejected(duration: object) -> None:
    with pytest.raises(ValueError):
        compiler.normalize_patch({'duration': duration})


def test_delay_round_trip_and_native_execution(tmp_path: Path) -> None:
    root = Path(__file__).parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    message = {
        'type': 'control-change',
        'output': 'din',
        'channel': 1,
        'controller': 7,
        'value': 100,
    }
    setup = {
        'patches': {
            'delayed': {
                'patches': [
                    {
                        'type': 'program-change',
                        'output': 'din',
                        'channel': 1,
                        'program': 7,
                    },
                    {'duration': '100 ms'},
                    message,
                ]
            },
            'independent': message,
        },
        'switches': {
            'hold': {
                'button': 'A',
                'mode': 'momentary',
                'on': {'patch': 'delayed'},
                'off': message | {'value': 0},
            }
        },
    }
    data = compiler.compile_show(setup, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['patches']['delayed']['patches'][1] == {'duration': 0.1}
    assert compiler.compile_show(exported, {}, [], target) == data
    setup['patches']['delayed']['patches'][1]['type'] = 'delay'
    assert compiler.compile_show(setup, {}, [], target) == data
    image = tmp_path / 'delay.bin'
    image.write_bytes(data)
    executable = tmp_path / 'delay-test'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_delays.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    native_tests.run_native([str(executable), str(image)], check=True)
