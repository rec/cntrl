"""Logical routes and transport roles come exclusively from a target descriptor."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, midi_endpoints, models, resolved
from scripts import native_tests


def test_file_and_clock_operations_use_declared_names_and_routes(
    tmp_path: Path,
) -> None:
    target = base_target().model_copy(
        update={
            'midi_endpoints': [
                models.MidiEndpoint(
                    name='computer',
                    label='Computer',
                    route=0,
                    transport='usb-device',
                    input=True,
                ),
                models.MidiEndpoint(name='synth', label='Synth', route=1, input=True),
            ],
            'default_midi_output': 'synth',
            'trs_ports': {},
        }
    )
    source = {
        'patches': {
            'play': {'operation': 'play-midi-file'},
            'record': {'operation': 'record-midi-file'},
        },
        'page_settings': {'clock_outputs': ['synth']},
    }
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['patches']['play']['outputs'] == ['synth']
    assert exported['patches']['record']['inputs'] == ['computer', 'synth']
    assert exported['page_settings']['clock_outputs'] == ['synth']
    assert compiler.compile_show(exported, {}, [], target) == data
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / 'receiver'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/topology_receiver.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    path = tmp_path / 'setup.bin'
    path.write_bytes(data)
    native_tests.run_native([str(executable), str(path)], check=True)
    with pytest.raises(compiler.CompilationError, match='unavailable MIDI file output'):
        compiler.compile_show(
            {
                'usb_device_midi': False,
                'patches': {
                    'play': {'operation': 'play-midi-file', 'outputs': ['computer']}
                },
            },
            {},
            [],
            target,
        )


def test_target_routes_must_be_dense_and_names_unambiguous() -> None:
    data = base_target().model_dump()
    data['midi_endpoints'] = [{'name': 'synth', 'label': 'Synth', 'route': 2}]
    data['default_midi_output'] = 'synth'
    with pytest.raises(ValueError, match='endpoint count'):
        models.Target.model_validate(data)


def base_target() -> models.Target:
    root = Path(__file__).resolve().parents[1]
    return models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )


def test_configurable_port_names_are_bound_by_explicit_slot() -> None:
    target = base_target().model_copy(
        update={
            'midi_endpoints': [
                models.MidiEndpoint(
                    name='auxiliary', label='Auxiliary', route=0, port=1
                ),
                models.MidiEndpoint(name='synth', label='Synth', route=1),
            ],
            'default_midi_output': 'synth',
            'trs_ports': {'auxiliary': ['midi-a', 'expression']},
        }
    )
    source = {
        'trs_ports': {'auxiliary': 'expression'},
        'page_settings': {
            'expression': {
                'auxiliary': {'output': 'synth', 'channel': 2, 'controller': 7},
            }
        },
    }
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['trs_ports'] == {'auxiliary': 'expression'}
    assert exported['page_settings']['expression']['auxiliary']['output'] == 'synth'
    assert compiler.compile_show(exported, {}, [], target) == data


def test_endpoint_padding_is_validated_before_export() -> None:
    data = bytearray(midi_endpoints.pack(base_target()))
    data[6 + 4 + 10] = 1
    with pytest.raises(ValueError, match='endpoint text'):
        midi_endpoints.unpack(bytes(data), 0)
