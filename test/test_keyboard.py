"""Keyboard patch authoring and portable key holds."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved
from scripts import native_tests


def test_keyboard_actions_round_trip_and_require_capability() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    setup = {
        'page_settings': {'clock_outputs': ['din']},
        'patch_cycles': {},
        'switches': {
            'A': {
                'button': 'A',
                'on': {'operation': 'keyboard', 'action': 'press', 'keys': [44]},
                'gestures': {
                    'release': {
                        'operation': 'keyboard',
                        'action': 'release',
                        'keys': [44],
                    }
                },
            }
        },
    }
    with pytest.raises(compiler.CompilationError, match='unavailable'):
        compiler.compile_show(setup, {}, [], target)
    target = target.model_copy(update={'functions': ['usb-hid-keyboard']})
    data = compiler.compile_show(setup, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['switches']['A']['on']['keys'] == [44]
    assert exported['switches']['A']['gestures']['release']['action'] == 'release'
    assert 'keyboard' not in exported['page_settings']
    assert all(p['type'] != 'keyboard' for p in exported['pages'].values())
    assert compiler.compile_show(exported, {}, [], target) == data


@pytest.mark.parametrize(
    'setup',
    [
        {'page_settings': {'keyboard': []}},
        {'pages': {'keyboard': {'type': 'keyboard'}}},
    ],
)
def test_keyboard_page_configuration_is_rejected(setup: dict[str, object]) -> None:
    with pytest.raises(ValueError):
        models.Setup.model_validate(setup)


@pytest.mark.parametrize('version', [36, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47])
def test_stored_keyboard_pages_require_recompilation(version: int) -> None:
    original = bytes.fromhex(Path(f'firmware/test/setup{version}.golden').read_text())
    with pytest.raises(ValueError, match='Keyboard page.*recompile'):
        resolved.upgrade(original)


@pytest.mark.parametrize('keys', [[], [0], [224], list(range(4, 11))])
def test_keyboard_rejects_invalid_key_reports(keys: list[int]) -> None:
    with pytest.raises(ValueError):
        models.Keyboard(operation='keyboard', action='press', keys=keys)


def test_keyboard_holds_release_and_reconnect(tmp_path: Path) -> None:
    peer = tmp_path / 'keyboard'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            '-Ifirmware/include',
            'firmware/test/test_keyboard.cpp',
            '-o',
            str(peer),
        ],
        check=True,
    )
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    ).model_copy(update={'functions': ['usb-hid-keyboard']})
    data = compiler.compile_show(
        {
            'patches': {
                'shortcut': {
                    'patches': [
                        {
                            'operation': 'keyboard',
                            'action': 'press',
                            'keys': [44],
                            'modifiers': 9,
                        },
                        {
                            'operation': 'keyboard',
                            'action': 'release',
                            'keys': [44],
                            'modifiers': 9,
                        },
                    ]
                },
            },
            'page_settings': {'clock_outputs': ['din']},
        },
        {},
        [],
        target,
    )
    fixture = tmp_path / 'keyboard.bin'
    fixture.write_bytes(data)
    native_tests.run_native([str(peer), str(fixture)], check=True)
