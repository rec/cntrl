"""Relay commands and pulse deadlines share the patch/configuration path."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved, songs
from scripts import native_tests


def target() -> models.Target:
    value = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    return value.model_copy(update={'relay_outputs': ['amp.tip', 'amp.ring']})


def relay(
    action: str, contact: str = 'amp.tip', **settings: object
) -> dict[str, object]:
    return {'operation': 'relay', 'relay': contact, 'action': action, **settings}


def setup() -> dict[str, object]:
    pulse = relay('pulse', duration='100 ms')
    return {
        'relays': {'amp.tip': False, 'amp.ring': True},
        'patches': {
            'engage': relay('engage'),
            'disengage': relay('disengage'),
            'toggle': relay('toggle'),
            'pulse': pulse,
            'ring': relay('pulse', 'amp.ring', duration='50 ms'),
            'blocked': {
                'patches': [
                    pulse,
                    {'controller': 7, 'value': 100, 'output': 'din', 'channel': 1},
                ]
            },
            'cancelled': {'patches': [pulse, {'duration': 1.0}]},
        },
        'patch_cycles': {'Relay': ['engage', 'pulse', 'toggle']},
    }


def test_relay_commands_round_trip_and_execute(tmp_path: Path) -> None:
    root = Path(__file__).parents[1]
    data = compiler.compile_show(setup(), {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['relays'] == {'amp.tip': False, 'amp.ring': True}
    assert exported['patches']['pulse']['duration'] == 0.1
    assert compiler.compile_show(exported, {}, [], target()) == data
    fixture = tmp_path / 'relays.bin'
    fixture.write_bytes(data)
    executable = tmp_path / 'relays'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-pedantic',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_relay_outputs.cpp'),
            '-o',
            str(executable),
        ]
    )
    native_tests.run_native([str(executable), str(fixture)])


def test_song_sources_preserve_relay_operations() -> None:
    library = songs.Library(
        patches=setup()['patches'],
        songs=[songs.Song(name='Song', patches=['pulse', 'engage'])],
    )
    restored = songs.decode(songs.encode(library, target()))
    assert restored.songs == library.songs
    assert restored.patches['pulse']['action'] == 'pulse'


@pytest.mark.parametrize(
    'patch',
    [
        relay('pulse'),
        relay('engage', duration='100 ms'),
        relay('pulse', duration='0 ms'),
        relay('pulse', duration='65.536 s'),
        relay('engage', 'unknown'),
        relay('invalid'),
    ],
)
def test_invalid_relay_commands_are_rejected(patch: dict[str, object]) -> None:
    with pytest.raises(ValueError):
        compiler.compile_show({'patches': {'test': patch}}, {}, [], target())


def test_unqualified_targets_reject_relay_configuration() -> None:
    unsupported = target().model_copy(update={'relay_outputs': []})
    for source in (
        {'relays': {'amp.tip': False}},
        {'patches': {'test': relay('engage')}},
    ):
        with pytest.raises(ValueError, match='unsupported'):
            compiler.compile_show(source, {}, [], unsupported)
