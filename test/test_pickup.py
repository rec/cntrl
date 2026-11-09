"""Pickup authoring and compiled policy round trips."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, pickup, resolved
from scripts import native_tests


@pytest.mark.parametrize(
    'value, expected',
    [(None, None), ('none', None), ('None', None), (False, False), (True, True)],
)
def test_pickup_accepts_only_nullable_booleans(
    value: object, expected: bool | None
) -> None:
    assert models.Setup.model_validate({'pickup': value}).pickup is expected
    assert compiler.normalize_patch({'patches': [], 'pickup': value}).pickup is expected


@pytest.mark.parametrize('value', [0, 1, 'true', 'false', 'NONE', '', []])
def test_pickup_rejects_implicit_boolean_coercion(value: object) -> None:
    with pytest.raises(ValueError):
        models.Setup.model_validate({'pickup': value})
    with pytest.raises(ValueError):
        compiler.normalize_patch({'patches': [], 'pickup': value})


def test_pickup_policy_round_trips(tmp_path: Path) -> None:
    root = Path(__file__).parents[1]
    executable = tmp_path / 'pickup'
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
            str(root / 'firmware/test/test_midi_pickup.cpp'),
            '-o',
            str(executable),
        ]
    )
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'firmware/test/target.json')
    )
    setup = {
        'pickup': False,
        'pickup_pairing_ms': 7,
        'patches': {
            'inherit': {'patches': [], 'pickup': 'None'},
            'acquire': {'patches': [], 'pickup': True},
            'direct': {'patches': [], 'pickup': False},
        },
    }
    for policy in (False, True, None):
        setup['pickup'] = policy
        data = compiler.compile_show(setup, {}, [], target)
        offset = data.index(b'CPUP')
        assert pickup.HEADER.unpack_from(data, offset)[2:] == (pickup.encode(policy), 7)
        exported = tomlkit.parse(resolved.export(data)).unwrap()
        assert exported.get('pickup') is policy
        assert exported['pickup_pairing_ms'] == 7
        assert compiler.compile_show(exported, {}, [], target) == data
        fixture = tmp_path / 'pickup.bin'
        fixture.write_bytes(data)
        native_tests.run_native(
            [str(executable), str(fixture), str(int(policy is not False))]
        )


@pytest.mark.parametrize('value', [-1, 65536, True, 1.5])
def test_pairing_window_requires_whole_milliseconds(value: object) -> None:
    with pytest.raises(ValueError):
        models.Setup.model_validate({'pickup_pairing_ms': value})


def test_explicit_inheritance_survives_reference_export() -> None:
    root = Path(__file__).parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    source = {
        'inline_patches': False,
        'pickup': False,
        'patches': {
            'base': {'patches': [], 'pickup': True},
            'inherit': {'patch': 'base', 'pickup': 'None'},
        },
    }
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['patches']['inherit']['pickup'] == 'none'
    assert compiler.compile_show(exported, {}, [], target) == data
