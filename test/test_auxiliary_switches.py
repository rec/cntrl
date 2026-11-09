"""TRS contacts use the existing switch assignment and cleanup paths."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved, songs
from scripts import native_tests


def target() -> models.Target:
    value = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    return value.model_copy(
        update={'trs_ports': {f'configurable-{i}': ['off', 'aux'] for i in (1, 2)}}
    )


def setup() -> dict[str, object]:
    return {
        'fast_switches': False,
        'trs_ports': {'configurable-1': 'aux', 'configurable-2': 'aux'},
        'auxiliary_switches': {'configurable-1': 'combined'},
        'switches': {
            'tip': {'button': 'configurable-1.tip', 'on': cc(1, 1)},
            'ring': {'button': 'configurable-1.ring', 'on': cc(2, 1)},
            'both': {
                'button': 'configurable-1.both',
                'mode': 'momentary',
                'on': cc(64, 127),
                'off': cc(64, 0),
            },
            'independent-tip': {'button': 'configurable-2.tip', 'on': cc(3, 1)},
            'independent-ring': {'button': 'configurable-2.ring', 'on': cc(4, 1)},
        },
        'patches': {
            'performance': {
                'bindings': {
                    'configurable-1.both': {
                        'mode': 'momentary',
                        'on': cc(64, 127),
                        'off': cc(64, 0),
                    },
                    'F': {'operation': 'back'},
                }
            }
        },
    }


def cc(controller: int, value: int) -> dict[str, object]:
    return {'controller': controller, 'value': value, 'channel': 1, 'output': 'din'}


def test_auxiliary_contacts_round_trip_and_execute(tmp_path: Path) -> None:
    root = Path(__file__).parents[1]
    data = compiler.compile_show(setup(), {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['auxiliary_switches'] == {'configurable-1': 'combined'}
    assert compiler.compile_show(exported, {}, [], target()) == data
    fixture = tmp_path / 'auxiliary.bin'
    fixture.write_bytes(data)
    executable = tmp_path / 'auxiliary'
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
            str(root / 'firmware/test/test_auxiliary_switches.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    native_tests.run_native([str(executable), str(fixture)], check=True)


def test_song_bindings_use_the_same_auxiliary_identifiers() -> None:
    library = songs.Library(
        patches=setup()['patches'],
        songs=[songs.Song(name='Song', patches=['performance'])],
    )
    restored = songs.decode(songs.encode(library, target()))
    assert restored.songs == library.songs
    assert (
        restored.patches['performance']['bindings']['configurable-1.both']['mode']
        == 'momentary'
    )


@pytest.mark.parametrize(
    'source',
    [
        {
            'trs_ports': {'configurable-1': 'aux'},
            'switches': {'x': {'button': 'configurable-1.both', 'on': cc(7, 1)}},
        },
        {'switches': {'x': {'button': 'configurable-1.tip', 'on': cc(7, 1)}}},
        {'auxiliary_switches': {'configurable-1': 'combined'}},
        {'auxiliary_switches': {'missing': 'combined'}},
    ],
)
def test_unsupported_auxiliary_assignments_are_rejected(
    source: dict[str, object],
) -> None:
    with pytest.raises(ValueError):
        compiler.compile_show(source, {}, [], target())
