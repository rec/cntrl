"""LFO authoring, full-resolution wire output and lifecycle regression fixtures."""

from pathlib import Path

import pytest
import tomlkit
from pydantic import ValidationError
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.patch_switcher import (
    compiler,
    lfos,
    midi_messages,
    midi_processing,
    models,
    resolved,
)
from cntrl.patch_switcher.models import MidiEndpoint
from scripts import native_tests


def configuration(**updates: object) -> dict[str, object]:
    return {
        'target': {'type': 'control-change', 'channel': 1, 'controller': 7},
        'shape': 'triangle',
        'range': [0, 127],
        'period': '1 s',
        'off': {'action': 'value', 'value': 0},
        **updates,
    }


def compile_lfo(
    value: dict[str, object], via: list[dict[str, object]] | None = None
) -> bytes:
    return midi_processing.compile_routes(
        {'sweep': {'generate': {'lfo': value}, 'to': ['din', 'trs'], 'via': via or []}},
        {},
        [
            MidiEndpoint(name=n, label=n, route=i, input=True)
            for i, n in enumerate(('din', 'trs'))
        ],
    )


@pytest.fixture(scope='module')
def runner(tmp_path_factory: pytest.TempPathFactory) -> Path:
    root = Path(__file__).parents[1]
    binary = tmp_path_factory.mktemp('lfos') / 'test-lfos'
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
            str(root / 'firmware/test/test_lfos.cpp'),
            '-o',
            str(binary),
        ]
    )
    return binary


def test_shapes_and_parameter_transactions(
    runner: Path, file_regression: FileRegressionFixture
) -> None:
    result = native_tests.run_native([str(runner)], capture_output=True, text=True)
    file_regression.check(result.stdout)


@pytest.mark.parametrize(
    'target,interval',
    [
        ({'type': 'control-change', 'channel': 1, 'controller': 7}, [0, 127]),
        (
            {'type': 'control-change', 'channel': 1, 'controller': 1, 'resolution': 14},
            [0, 16383],
        ),
        ({'type': 'pitch-bend', 'channel': 1}, [-8192, 8191]),
        ({'type': 'channel-pressure', 'channel': 1}, [0, 127]),
        ({'type': 'polyphonic-key-pressure', 'channel': 1, 'note': 60}, [0, 127]),
        ({'type': 'nrpn', 'channel': 1, 'parameter': 123, 'resolution': 7}, [0, 127]),
        (
            {'type': 'nrpn', 'channel': 1, 'parameter': 123, 'resolution': 14},
            [0, 16383],
        ),
        ({'type': 'rpn', 'channel': 1, 'parameter': 1}, [0, 16383]),
        ({'type': 'program-change', 'channel': 1}, [1, 128]),
    ],
)
def test_native_output_resolution(
    target: dict[str, object],
    interval: list[int],
    runner: Path,
    tmp_path: Path,
    file_regression: FileRegressionFixture,
) -> None:
    final = 1 if target['type'] == 'program-change' else 0
    value = configuration(
        target=target, range=interval, off={'action': 'value', 'value': final}
    )
    path = tmp_path / 'lfo.bin'
    path.write_bytes(compile_lfo(value))
    result = native_tests.run_native(
        [str(runner), str(path), 'trace'], capture_output=True, text=True
    )
    file_regression.check(result.stdout)


@pytest.mark.parametrize(
    'mode,updates',
    [
        ('trace', {'off': {'action': 'hold'}}),
        ('trace', {'off': {'action': 'restore'}}),
        ('missing-restore', {'off': {'action': 'restore'}}),
        ('blocked', {}),
        ('replace', {}),
        ('manual', {}),
        ('gate', {'phase_mode': 'retrigger'}),
        ('gate', {'phase_mode': 'free'}),
        ('once', {'run': 'once', 'shape': 'saw-up'}),
        ('clock', {'period': {'beats': '1'}}),
        (
            'partial-stop',
            {
                'target': {
                    'type': 'control-change',
                    'channel': 1,
                    'controller': 1,
                    'resolution': 14,
                },
                'range': [0, 16383],
            },
        ),
    ],
)
def test_native_lifecycle(
    mode: str,
    updates: dict[str, object],
    runner: Path,
    tmp_path: Path,
    file_regression: FileRegressionFixture,
) -> None:
    path = tmp_path / 'lfo.bin'
    path.write_bytes(compile_lfo(configuration(**updates)))
    result = native_tests.run_native(
        [str(runner), str(path), mode], capture_output=True, text=True
    )
    file_regression.check(result.stdout)


@pytest.mark.parametrize(
    'updates',
    [
        {'target': {'type': 'control-change', 'channel': 1, 'controller': 64}},
        {
            'target': {
                'type': 'control-change',
                'channel': 1,
                'controller': 74,
                'resolution': 14,
            }
        },
        {'target': {'type': 'rpn', 'channel': 1, 'parameter': 0}},
        {'target': {'type': 'pitch-bend', 'channel': 1, 'controller': 7}},
        {'range': [-1, 127]},
        {'off': {'action': 'restore', 'value': 0}},
        {'off': {'action': 'value'}},
        {'period': 0},
        {'period': {'beats': '0'}},
        {'run': 'once', 'phase_mode': 'free'},
        {'phase': 1},
        {'update_interval': '0.1 ms'},
    ],
)
def test_invalid_lfo_is_rejected(updates: dict[str, object]) -> None:
    with pytest.raises(ValidationError):
        lfos.Lfo.model_validate(configuration(**updates))


def test_lfo_collision_is_rejected() -> None:
    value = {'generate': {'lfo': configuration()}, 'to': 'din'}
    with pytest.raises(ValueError, match='overlapping'):
        midi_processing.compile_routes(
            {'a': value, 'b': value},
            {},
            [MidiEndpoint(name='din', label='DIN', route=0)],
        )


def test_saturated_outputs_preserve_progress_and_bounded_storage(
    runner: Path, tmp_path: Path
) -> None:
    routes: dict[str, object] = {
        f'lfo{i}': {
            'generate': {
                'lfo': configuration(
                    target={'type': 'polyphonic-key-pressure', 'channel': 1, 'note': i}
                )
            },
            'to': ['din', 'trs'],
        }
        for i in range(16)
    }
    routes['notes'] = {'from': 'din', 'to': 'trs'}
    data = midi_processing.compile_routes(
        routes,
        {},
        [
            MidiEndpoint(name=n, label=n, route=i, input=True)
            for i, n in enumerate(('din', 'trs'))
        ],
    )
    path = tmp_path / 'stress.bin'
    path.write_bytes(data)
    result = native_tests.run_native(
        [str(runner), str(path), 'stress'], capture_output=True, text=True
    )
    assert result.stdout.startswith('retained storage: ')


@pytest.mark.parametrize(
    'message',
    [
        {'type': 'control-change', 'controller': 1, 'value': 12345, 'resolution': 14},
        {'type': 'nrpn', 'parameter': 123, 'value': 42, 'resolution': 7},
        {'type': 'rpn', 'parameter': 1, 'value': 8192},
    ],
)
def test_full_setup_and_parameter_patches_round_trip(
    message: dict[str, object], runner: Path, tmp_path: Path
) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    show = {
        'midi': {'sweep': {'generate': {'lfo': configuration()}, 'to': 'trs'}},
        'patches': {'sound': {'output': 'din', 'channel': 1, **message}},
        'patch_cycles': {'Show': ['sound']},
    }
    data = compiler.compile_show(show, {}, [], target)
    restored = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(restored, {}, [], target) == data
    path = tmp_path / 'setup.bin'
    path.write_bytes(data)
    native_tests.run_native([str(runner), str(path), 'document'])


@pytest.mark.parametrize(
    'via',
    [
        [{'map': {'set': {'channel': {'from': 'value'}}}}],
        [{'scale': {'field': 'note', 'output': [0, 127]}}],
        [{'delay': {'time': '1 s', 'repeats': 2}}],
    ],
)
def test_ambiguous_lfo_processing_is_rejected(via: list[dict[str, object]]) -> None:
    with pytest.raises(ValueError, match='LFO'):
        compile_lfo(configuration(), via)


def test_fine_values_are_scaled_before_encoding(runner: Path, tmp_path: Path) -> None:
    value = configuration(
        target={
            'type': 'control-change',
            'channel': 1,
            'controller': 1,
            'resolution': 14,
        },
        range=[0, 16383],
    )
    path = tmp_path / 'scaled.bin'
    path.write_bytes(
        compile_lfo(
            value,
            [
                {'scale': {'field': 'value', 'output': [0, 8191]}},
                {'map': {'set': {'controller': 2}}},
            ],
        )
    )
    result = native_tests.run_native(
        [str(runner), str(path), 'trace'], capture_output=True, text=True
    )
    first_output = '\n'.join(
        x for x in result.stdout.splitlines() if x.split()[1] == '0'
    )
    assert '250000 0 b0 02 20\n250000 0 b0 22 00' in first_output
    assert '500000 0 b0 02 3f\n500000 0 b0 22 7f' in first_output


@pytest.mark.parametrize('payload', [b'', bytes(5), bytes(6), bytes(15), bytes(18)])
def test_malformed_parameter_transactions_are_rejected(payload: bytes) -> None:
    with pytest.raises(ValueError):
        midi_messages.decode_parameter(payload)


def test_reenable_waits_for_blocked_final_write(runner: Path, tmp_path: Path) -> None:
    path = tmp_path / 'restart.bin'
    path.write_bytes(compile_lfo(configuration(off={'action': 'value', 'value': 17})))
    result = native_tests.run_native(
        [str(runner), str(path), 'restart-stopping'], capture_output=True, text=True
    )
    values = [
        int(x.split()[-1], 16)
        for x in result.stdout.splitlines()
        if x.split()[1] == '0'
    ]
    assert values == [0, 64, 17, 0, 17]
