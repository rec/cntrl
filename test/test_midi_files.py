"""MIDI file patch authoring and compiled round trips."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )


def test_midi_file_operations_round_trip(target: models.Target) -> None:
    setup = {
        'patches': {
            'play': {
                'operation': 'play-midi-file',
                'filename': '255-3.mid',
                'outputs': ['din', 'trs'],
                'loops': 0,
                'pre_delay': '2 seconds',
                'post_delay': 3,
                'speed': 1.5,
                'reverse': True,
                'transpose': -12,
            },
            'record': {
                'operation': 'record-midi-file',
                'channels': [1, 16],
                'inputs': ['din', 'usb-host'],
            },
        },
        'patch_cycles': {'Files': ['play', 'record']},
    }
    data = compiler.compile_show(setup, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    actual = models.Setup.model_validate(exported)
    expected = models.Setup.model_validate(setup)
    assert {k: compiler.normalize_patch(v) for k, v in actual.patches.items()} == {
        k: compiler.normalize_patch(v) for k, v in expected.patches.items()
    }
    assert compiler.compile_show(exported, {}, [], target) == data


def test_midi_file_defaults() -> None:
    play = models.PlayMidiFile(operation='play-midi-file')
    record = models.RecordMidiFile(operation='record-midi-file')
    assert play.filename == '' and play.outputs == ['din'] and play.loops == 1
    assert play.speed == 1 and play.pre_delay == play.post_delay == play.transpose == 0
    assert record.channels == list(range(1, 17))
    assert record.inputs == ['din', 'trs', 'usb-device', 'usb-host']


@pytest.mark.parametrize(
    'fields',
    [
        {'filename': '../unsafe.mid'},
        {'outputs': []},
        {'outputs': ['din', 'din']},
        {'loops': -1},
        {'pre_delay': 0.5},
        {'post_delay': '1 Hz'},
        {'speed': 0},
        {'speed': float('inf')},
        {'transpose': 128},
    ],
)
def test_invalid_playback_is_rejected(fields: dict[str, object]) -> None:
    with pytest.raises(ValueError):
        models.PlayMidiFile.model_validate({'operation': 'play-midi-file', **fields})


def test_unavailable_file_output_is_rejected(target: models.Target) -> None:
    with pytest.raises(compiler.CompilationError, match='unavailable MIDI file output'):
        compiler.compile_show(
            {
                'patches': {
                    'play': {'operation': 'play-midi-file', 'outputs': ['missing']}
                }
            },
            {},
            [],
            target,
        )


@pytest.mark.parametrize(
    'output,setting', [('usb-device', 'usb_device_midi'), ('usb-host', 'usb_host_midi')]
)
def test_file_output_respects_usb_enable_flags(
    target: models.Target, output: str, setting: str
) -> None:
    capable = target.model_copy(update={'outputs': [*target.outputs, output]})
    with pytest.raises(compiler.CompilationError, match='unavailable MIDI file output'):
        compiler.compile_show(
            {
                setting: False,
                'patches': {
                    'play': {'operation': 'play-midi-file', 'outputs': [output]}
                },
            },
            {},
            [],
            capable,
        )
