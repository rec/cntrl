"""Patch control maps share the normal graph, gestures and switch lifetimes."""

import subprocess
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved, songs
from scripts import native_tests


@pytest.fixture
def binding_runner(tmp_path: Path) -> Path:
    root = Path(__file__).parents[1]
    executable = tmp_path / 'bindings'
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
            str(root / 'firmware/test/test_control_bindings.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    return executable


def test_patch_bindings_round_trip_and_execute(
    tmp_path: Path, binding_runner: Path
) -> None:
    target = binding_target()
    data = compiler.compile_show(setup(), {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['patches']['chorus']['parent'] == 'verse'
    assert compiler.compile_show(exported, {}, [], target) == data
    fixture = tmp_path / 'bindings.bin'
    fixture.write_bytes(data)
    edited = tmp_path / 'edited.bin'
    subprocess.run([str(binding_runner), str(fixture), str(edited)], check=True)
    saved = tomlkit.parse(resolved.export(edited.read_bytes())).unwrap()
    assert saved['patches']['program_louder'] == {'parent': 'program', 'volume': 101}


def test_song_patch_names_inherit_bindings() -> None:
    target = binding_target()
    library = songs.Library(
        instruments=setup()['instruments'],
        patches=setup()['patches'],
        songs=[songs.Song(name='Song', patches=['verse', 'chorus', 'verse'])],
    )
    restored = songs.decode(songs.encode(library, target))
    assert restored.songs == library.songs
    assert restored.patches['verse']['bindings']['F'] == {'operation': 'back'}


def test_imported_maps_preserve_local_control_references(
    tmp_path: Path, binding_runner: Path
) -> None:
    target = binding_target()
    data = compiler.compile_show(
        {
            'patches': {'boost': cc(7, 0), 'chorus': cc(7, 0)},
            'patch_cycles': {'Show': [{'document': 'source', 'patch': 'verse'}]},
            'trs_ports': {'configurable-1': 'expression'},
        },
        {},
        [],
        target,
        documents={'source': setup()},
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    compiled = compiler.compile_show(exported, {}, [], target)
    # Exported imported maps remain usable without the original config files.
    assert resolved.graph_offset(compiled)
    bound = exported['patch_cycles']['Show'][0]['patches'][0]['bindings']
    assert bound['D']['name'] == 'boost'
    fixture = tmp_path / 'imported.bin'
    fixture.write_bytes(compiled)
    subprocess.run([str(binding_runner), str(fixture), '--validate'], check=True)


@pytest.mark.parametrize(
    'binding',
    [
        {'Z': 'boost'},
        {'A': 'absent'},
        {'A': {'mode': 'momentary', 'on': 'boost'}},
        {'A': {'on': 'chorus', 'gestures': {}}},
        {'configurable-2': {'output': 'din', 'channel': 1, 'controller': 11}},
    ],
)
def test_invalid_bindings_are_rejected(binding: dict[str, object]) -> None:
    target = binding_target()
    value = setup()
    value['patches']['verse']['bindings'] = binding
    with pytest.raises(ValueError):
        compiler.compile_show(value, {}, [], target)


@pytest.mark.parametrize('patch', [{'note': 60, 'velocity': 80}, {'value': 0}])
def test_midi_shorthand_can_also_install_a_map(patch: dict[str, object]) -> None:
    value = {
        'instruments': {
            'guitar': {'instrument': 'guitar', 'channel': 1, 'output': 'din'}
        },
        'patches': {'test': patch | {'bindings': {'F': {'operation': 'back'}}}},
        'patch_cycles': {'Show': ['test']},
    }
    data = compiler.compile_show(value, {}, [], binding_target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], binding_target()) == data


def test_binding_can_derive_a_controller_without_repeating_its_value() -> None:
    value = {
        'patches': {
            'boost': cc(7, 100),
            'test': {'bindings': {'A': {'parent': 'boost', 'controller': 11}}},
        }
    }
    data = compiler.compile_show(value, {}, [], binding_target())
    assert (
        compiler.compile_show(
            tomlkit.parse(resolved.export(data)).unwrap(), {}, [], binding_target()
        )
        == data
    )


def test_unavailable_live_sources_release_the_failed_map(
    tmp_path: Path, binding_runner: Path
) -> None:
    data = compiler.compile_show(
        {
            'inline_patches': False,
            'patches': {
                'bad': {'bindings': {'A': {'document': 'foreign', 'patch': 'boost'}}}
            },
        },
        {},
        [],
        binding_target(),
        documents={'foreign': {'patches': {'boost': cc(7, 100)}}},
    )
    fixture = tmp_path / 'unavailable.bin'
    fixture.write_bytes(data)
    subprocess.run([str(binding_runner), str(fixture), '--reject'], check=True)


def setup() -> dict[str, object]:
    return {
        'instruments': {
            'guitar': {'instrument': 'guitar', 'output': 'din', 'channel': 1}
        },
        'trs_ports': {'configurable-1': 'expression'},
        'patches': {
            'boost': cc(7, 100),
            'verse': cc(7, 30)
            | {
                'bindings': {
                    'A': {'mode': 'momentary', 'on': cc(64, 127), 'off': cc(64, 0)},
                    'B': {'on': cc(10, 1), 'gestures': {'release': cc(10, 2)}},
                    'C': 'chorus',
                    'D': 'boost',
                    'E': {'on': cc(11, 1), 'gestures': {'double_tap': cc(11, 2)}},
                    'F': {'operation': 'back'},
                    'G': 'boost',
                    'H': 'boost',
                    'configurable-1': {'output': 'din', 'channel': 2, 'controller': 11},
                }
            },
            'chorus': {'parent': 'verse', 'value': 90},
            'empty': {'bindings': {}},
            'program': {
                'program': 1,
                'volume': 90,
                'bindings': {'F': {'operation': 'back'}},
            },
            'program_louder': {'parent': 'program', 'volume': 100},
        },
        'patch_cycles': {
            'Song': ['verse', 'chorus', 'empty', 'program', 'program_louder']
        },
    }


def cc(controller: int, value: int) -> dict[str, object]:
    return {'controller': controller, 'value': value, 'output': 'din', 'channel': 1}


@pytest.mark.parametrize('shifted', [False, True])
@pytest.mark.parametrize('local', [False, True])
def test_long_double_actions_round_trip_in_each_control_layer(
    tmp_path: Path, binding_runner: Path, shifted: bool, local: bool
) -> None:
    action = {
        'on': cc(7, 10),
        'gestures': {'long_double_tap': cc(7, 100), 'long_double_release': cc(7, 0)},
    }
    if shifted:
        action = {'on': cc(7, 10), 'shifted': action}
    source = (
        {'patches': {'test': {'bindings': {'A': action}}}}
        if local
        else {'switches': {'test': action | {'button': 'A'}}}
    )
    target = binding_target()
    data = compiler.compile_show(source, {}, [], target)
    assert (
        compiler.compile_show(
            tomlkit.parse(resolved.export(data)).unwrap(), {}, [], target
        )
        == data
    )
    if local:
        data = compiler.compile_show(
            {'patch_cycles': {'Show': [{'document': 'source', 'patch': 'test'}]}},
            {},
            [],
            target,
            documents={'source': source},
        )
    fixture = tmp_path / 'long-double.bin'
    fixture.write_bytes(data)
    subprocess.run([str(binding_runner), str(fixture), '--validate'], check=True)


def binding_target() -> models.Target:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    return target.model_copy(
        update={
            'trs_ports': {'configurable-1': ['expression', 'off']},
            'buttons': ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'],
        }
    )
