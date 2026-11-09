"""Compile authored routes through editable export and the native patch entry."""

import struct
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit

from cntrl.patch_switcher import (
    compiler,
    display,
    midi_processing,
    models,
    resolved,
    songs,
)
from scripts import native_tests


def target() -> models.Target:
    result = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    return result.model_copy(
        update={
            'midi_endpoints': [
                e.model_copy(update={'output': True}) if e.name == 'usb-host' else e
                for e in result.midi_endpoints
            ],
            'trs_ports': {'configurable-1': ['off', 'expression', 'midi-a']},
        }
    )


def setup() -> dict[str, object]:
    return {
        'midi': {'keys': {'from': 'din', 'to': 'trs'}},
        'patches': {
            'base': {
                'midi': {
                    'octave': {
                        'from': 'system/keys',
                        'via': [{'transpose': 12}],
                        'to': 'usb-host',
                    },
                    'controls': {
                        'from': 'din',
                        'via': [{'keep': {'type': 'control-change'}}],
                        'to': 'din',
                    },
                },
            },
            'variation': {
                'parent': 'base',
                'midi': {
                    'octave': {
                        'from': 'system/keys',
                        'via': [{'transpose': 7}],
                        'to': 'usb-host',
                    }
                },
            },
            'alias': {'patch': 'base'},
            'clear': {'midi': {}},
            'boost': {'controller': 7, 'value': 127, 'output': 'din', 'channel': 1},
        },
        'patch_cycles': {'Show': ['base', 'variation', 'alias', 'clear']},
    }


@pytest.fixture
def stream_runner(tmp_path: Path) -> Path:
    root = Path(__file__).parents[1]
    executable = tmp_path / 'streams'
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
            str(root / 'firmware/test/test_midi_streams.cpp'),
            '-o',
            str(executable),
        ]
    )
    return executable


def test_patch_streams_round_trip_and_execute_normal_entry(
    tmp_path: Path, stream_runner: Path
) -> None:
    data = compiler.compile_show(setup(), {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['midi'] == setup()['midi']
    assert exported['patches']['variation']['parent'] == 'base'
    assert (
        exported['patches']['variation']['midi']
        == setup()['patches']['variation']['midi']
    )
    assert compiler.compile_show(exported, {}, [], target()) == data
    fixture = tmp_path / 'streams.bin'
    fixture.write_bytes(data)
    native_tests.run_native([str(stream_runner), str(fixture)])


def test_explicit_empty_matrix_survives_export() -> None:
    data = compiler.compile_show({'midi': {}}, {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['midi'] == {}
    assert compiler.compile_show(exported, {}, [], target()) == data


def test_route_only_patch_infers_empty_entry_sequence() -> None:
    data = compiler.compile_show(
        {
            'patches': {
                'octave': {
                    'midi': {
                        'notes': {
                            'from': 'din',
                            'via': [{'transpose': 12}],
                            'to': 'trs',
                        }
                    }
                }
            }
        },
        {},
        [],
        target(),
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['patches']['octave']['midi']['notes']['via'] == [{'transpose': 12}]
    assert compiler.compile_show(exported, {}, [], target()) == data


def test_derived_routes_replace_by_name_and_inherit_other_routes() -> None:
    source = setup()
    patches = source['patches']
    value = compiler.resolve_derivation(
        compiler.normalize_patch(patches['variation']), patches, 'variation'
    )
    assert value.midi == patches['base']['midi'] | patches['variation']['midi']
    assert value.midi['octave']['via'] == [{'transpose': 7}]
    data = compiler.compile_show(source, {}, [], target())
    offset = data.index(b'CSTR')
    _, _, count, explicit = midi_processing.HEADER.unpack_from(data, offset)
    assert explicit == 1
    assert count == 5


def test_inactive_configurable_port_name_stays_reserved() -> None:
    with pytest.raises(compiler.CompilationError, match='reserved MIDI route name'):
        compiler.compile_show(
            {
                'trs_ports': {'configurable-1': 'off'},
                'midi': {'configurable-1': {'from': 'din', 'to': 'trs'}},
            },
            {},
            [],
            target(),
        )


def test_configurable_input_cannot_be_used_in_expression_mode() -> None:
    configured = target().model_copy(
        update={
            'midi_endpoints': [
                e.model_copy(update={'input': True})
                if e.name == 'configurable-1'
                else e
                for e in target().midi_endpoints
            ]
        }
    )
    with pytest.raises(compiler.CompilationError, match='not an input'):
        compiler.compile_show(
            {
                'trs_ports': {'configurable-1': 'off'},
                'midi': {'notes': {'from': 'configurable-1', 'to': 'trs'}},
            },
            {},
            [],
            configured,
        )


def test_song_library_retains_patch_routes_and_derived_replacements() -> None:
    library = songs.Library(
        patches=setup()['patches'],
        midi=setup()['midi'],
        songs=[songs.Song(name='Song', patches=['base', 'variation', 'alias'])],
    )
    restored = songs.decode(songs.encode(library, target()))
    assert restored.songs == library.songs
    assert restored.midi == library.midi
    assert restored.patches['base']['midi'] == library.patches['base']['midi']
    assert restored.patches['variation']['midi'] == library.patches['variation']['midi']
    assert restored.patches['variation']['parent'] == 'base'


@pytest.fixture
def wrapped_program() -> bytes:
    return compiler.compile_show(
        {
            'instruments': {
                'guitar': {'instrument': 'guitar', 'output': 'din', 'channel': 1}
            },
            'patches': {
                'program': {
                    'program': 1,
                    'volume': 90,
                    'midi': {
                        'notes': {
                            'from': 'din',
                            'via': [{'transpose': 12}],
                            'to': 'trs',
                        }
                    },
                    'bindings': {'F': {'operation': 'back'}},
                },
                'louder': {'parent': 'program', 'volume': 101},
            },
            'patch_cycles': {'Show': ['program', 'louder']},
        },
        {},
        [],
        target(),
    )


def test_derived_program_with_routes_and_bindings_round_trips(
    wrapped_program: bytes,
) -> None:
    exported = tomlkit.parse(resolved.export(wrapped_program)).unwrap()
    assert exported['patches']['louder'] == {'parent': 'program', 'volume': 101}
    assert exported['patches']['program']['midi']['notes']['via'] == [{'transpose': 12}]
    assert exported['patches']['program']['bindings']['F']['operation'] == 'back'
    assert compiler.compile_show(exported, {}, [], target()) == wrapped_program


def test_derived_export_rejects_cyclic_wrappers(wrapped_program: bytes) -> None:
    graph = resolved.graph_offset(wrapped_program)
    metadata = display.read_source(wrapped_program, graph)['patch_metadata']
    node = next(
        int(k)
        for k, v in metadata.items()
        if v.get('_derived', {}).get('parent') == 'program'
    )
    address = (
        graph
        + struct.unpack_from(
            '<I', wrapped_program, graph + compiler.GRAPH.size + node * 4
        )[0]
    )
    damaged = bytearray(wrapped_program)
    struct.pack_into('<H', damaged, address + 4, node)
    struct.pack_into('<I', damaged, 20, crc32(damaged[28:]))
    with pytest.raises(ValueError, match='cyclic Patch graph'):
        resolved.export(bytes(damaged))


def test_stream_gate_operation_round_trips() -> None:
    source = setup()
    source['patches']['mute'] = {
        'operation': 'midi-stream',
        'stream': 'system/keys',
        'enabled': False,
    }
    data = compiler.compile_show(source, {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['patches']['mute'] == source['patches']['mute']
    assert compiler.compile_show(exported, {}, [], target()) == data


@pytest.mark.parametrize('reference', ['system/missing', 'missing'])
def test_unknown_stream_gate_is_rejected(reference: str) -> None:
    source = setup()
    source['patches']['mute'] = {
        'operation': 'midi-stream',
        'stream': reference,
        'enabled': False,
    }
    with pytest.raises(compiler.CompilationError, match='unknown MIDI stream'):
        compiler.compile_show(source, {}, [], target())
