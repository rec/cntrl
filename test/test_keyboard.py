"""Keyboard page authoring and portable key ownership."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved
from scripts import native_tests


def test_keyboard_settings_round_trip_and_require_capability() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    shortcuts = [
        {'name': 'Play', 'keys': [44]},
        {'name': 'Record', 'keys': [21], 'modifiers': 9},
    ]
    setup = {
        'page_settings': {'keyboard': shortcuts, 'clock_outputs': ['din']},
        'patch_cycles': {},
        'switches': {
            'A': {
                'button': 'A',
                'on': {'operation': 'undo'},
                'gestures': {'release': {'operation': 'brightness', 'value': 192}},
            }
        },
    }
    with pytest.raises(compiler.CompilationError, match='unavailable'):
        compiler.compile_show(setup, {}, [], target)
    target = target.model_copy(update={'functions': ['usb-hid-keyboard']})
    data = compiler.compile_show(setup, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['page_settings']['keyboard'][0]['keys'] == [44]
    assert exported['page_settings']['keyboard'][1]['modifiers'] == 9
    assert compiler.compile_show(exported, {}, [], target) == data


@pytest.mark.parametrize('count', [0, 1, 12])
def test_keyboard_storage_grows_with_configured_shortcuts(count: int) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    ).model_copy(update={'functions': ['usb-hid-keyboard']})
    shortcuts = [{'name': f'Shortcut {i}', 'keys': [44]} for i in range(count)]
    baseline = compiler.compile_show({}, {}, [], target)
    data = compiler.compile_show(
        {'page_settings': {'keyboard': shortcuts}}, {}, [], target
    )
    assert len(data) - len(baseline) == (6 + count * 39 if count else 0)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert len(exported['page_settings'].get('keyboard', [])) == count
    assert compiler.compile_show(exported, {}, [], target) == data


@pytest.mark.parametrize('keys', [[], [0], [224], list(range(4, 11))])
def test_keyboard_rejects_invalid_key_reports(keys: list[int]) -> None:
    with pytest.raises(ValueError):
        models.KeyboardShortcut(name='Test', keys=keys)


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
            'page_settings': {
                'keyboard': [
                    {'name': f'Play {i}', 'keys': [44], 'modifiers': 9}
                    for i in range(12)
                ],
                'clock_outputs': ['din'],
            }
        },
        {},
        [],
        target,
    )
    fixture = tmp_path / 'keyboard.bin'
    fixture.write_bytes(data)
    native_tests.run_native([str(peer), str(fixture)], check=True)
