"""Physical input bindings use declared identifiers and independent switch slots."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, inputs, models, resolved
from scripts import native_tests


def target() -> models.Target:
    root = Path(__file__).parents[1]
    source = compiler.read_document(root / 'firmware/test/target.json')
    assert isinstance(source, dict)
    source.update(
        buttons=['Less', 'More', 'Yes', 'Back', 'Top', 'Side', 'Aux', 'Spare'],
        controls={'next': 'More', 'confirm': 'Yes', 'back': 'Back'},
        functions=['stock-firmware'],
    )
    return models.Target.model_validate(source)


def test_declared_labels_and_high_input_bits_round_trip_and_execute(
    tmp_path: Path,
) -> None:
    def cc(controller: int, value: int) -> dict[str, object]:
        return {'output': 'din', 'channel': 1, 'controller': controller, 'value': value}

    show = {
        'patches': {
            'stock': {'operation': 'stock-firmware', 'exit_buttons': 'Spare+Less'},
            'default': {'operation': 'stock-firmware'},
        },
        'switches': {
            'last': {
                'button': 'Spare',
                'on': cc(7, 11),
                'gestures': {'release': cc(7, 12)},
                'enter': {'action': cc(20, 13)},
                'exit': cc(20, 14),
                'shifted': {'on': cc(7, 15)},
            },
            'side': {
                'button': 'Side',
                'on': cc(8, 21),
                'enter': {'action': cc(21, 23)},
            },
        },
    }
    capabilities = target()
    data = compiler.compile_show(show, {}, [], capabilities)
    assert resolved.input_labels(data) == capabilities.buttons
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['switches']['last']['button'] == 'Spare'
    assert exported['switches']['side']['button'] == 'Side'
    assert exported['patches']['stock']['exit_buttons'] == 'Less+Spare'
    assert exported['patches']['default']['exit_buttons'] == 'More+Yes'
    assert compiler.compile_show(exported, {}, [], capabilities) == data
    root = Path(__file__).parents[1]
    image = tmp_path / 'inputs.bin'
    image.write_bytes(data)
    program = tmp_path / 'inputs'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_input_bindings.cpp'),
            '-o',
            str(program),
        ],
        check=True,
    )
    native_tests.run_native([str(program), str(image)], check=True)


def test_back_control_remains_reserved_and_unknown_labels_are_rejected() -> None:
    for button in ('Back', 'Unknown'):
        with pytest.raises(compiler.CompilationError, match='button'):
            compiler.compile_show(
                {
                    'switches': {
                        'a': {
                            'button': button,
                            'on': {'operation': 'brightness', 'value': 1},
                        }
                    }
                },
                {},
                [],
                target(),
            )
    with pytest.raises(compiler.CompilationError, match='exit buttons unavailable'):
        compiler.compile_show(
            {'patches': {'stock': {'operation': 'stock-firmware'}}},
            {},
            [],
            target().model_copy(update={'controls': models.TargetControls()}),
        )


@pytest.mark.parametrize(
    'labels',
    [['duplicate', 'duplicate'], ['bad+id'], ['x' * 32], [str(i) for i in range(9)]],
)
def test_input_identifier_limits_follow_the_wire_format(labels: list[str]) -> None:
    source = target().model_dump()
    source.update(buttons=labels, controls={})
    with pytest.raises(ValueError):
        models.Target.model_validate(source)


def test_input_section_rejects_duplicate_ids() -> None:
    data = inputs.HEADER.pack(b'CINP', 2) + inputs.RECORD.pack(b'Yes') * 2
    with pytest.raises(ValueError, match='duplicate'):
        inputs.unpack(data, 0)
