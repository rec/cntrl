"""Arpeggiator profiles survive the normal setup compile and editable export."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import arpeggiator, compiler, models, resolved
from scripts import native_tests


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'firmware/test/target.json')
    )


@pytest.fixture(scope='module')
def document_receiver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    root = Path(__file__).parents[1]
    executable = tmp_path_factory.mktemp('arpeggiator') / 'document'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_arpeggiator_document.cpp'),
            '-o',
            str(executable),
        ]
    )
    return executable


@pytest.mark.parametrize('mode', ['poly', 'mono'])
def test_compiled_profiles_load_in_the_firmware(
    mode: str, target: models.Target, document_receiver: Path, tmp_path: Path
) -> None:
    data = compiler.compile_show(
        {
            'pages': {'arp': {'type': 'arpeggiator'}},
            'page_settings': {'arpeggiator': {'mode': mode}},
        },
        {},
        [],
        target,
    )
    image = tmp_path / 'setup.bin'
    image.write_bytes(data)
    native_tests.run_native([str(document_receiver), str(image)])


@pytest.mark.parametrize(
    'mode,kind',
    [
        (m, k)
        for m in ('poly', 'mono')
        for k in arpeggiator.KINDS
        if not (
            k == 'played'
            and m != 'mono'
            or k in ('ascending', 'descending')
            and m != 'poly'
        )
    ],
)
def test_profiles_round_trip(mode: str, kind: str, target: models.Target) -> None:
    source = dict(
        pages={'arp': {'type': 'arpeggiator'}},
        page_settings={
            'arpeggiator': {
                'mode': mode,
                'inputs': ['din'],
                'outputs': ['din'],
                'selection': {'kind': kind},
            }
        },
    )
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data
    assert exported['page_settings']['arpeggiator']['selection']['kind'] == kind


@pytest.mark.parametrize(
    'mode,kind', [('poly', 'played'), ('mono', 'ascending'), ('mono', 'descending')]
)
def test_selection_rejects_an_incompatible_bank_mode(
    mode: str, kind: str, target: models.Target
) -> None:
    with pytest.raises(ValueError, match='selection does not match its bank mode'):
        compiler.compile_show(
            {
                'pages': {'arp': {'type': 'arpeggiator'}},
                'page_settings': {
                    'arpeggiator': {'mode': mode, 'selection': {'kind': kind}}
                },
            },
            {},
            [],
            target,
        )


@pytest.mark.parametrize(
    'selection,expected_kind',
    [
        ({'kind': 'alternating', 'repeat_begin': True, 'repeat_end': True}, 3),
        ({'kind': 'walk', 'cycle': True, 'prob_up': '1/4', 'prob_down': '1/8'}, 5),
        ({'kind': 'played', 'direction': 'reverse'}, 2),
        ({'kind': 'mod', 'modulus': 5}, 6),
        ({'kind': 'shuffle'}, 7),
    ],
)
def test_nondefault_settings_survive_compilation_and_editable_export(
    selection: dict[str, object],
    expected_kind: int,
    target: models.Target,
    document_receiver: Path,
    tmp_path: Path,
) -> None:
    settings = {
        'mode': 'mono' if selection['kind'] == 'played' else 'poly',
        'selection': selection,
        'gate': '5/8',
        'rhythm': {'step': '3/8 beat'},
        'seed': 8675309,
        'events': 73,
        'inputs': ['din'],
        'outputs': ['din'],
    }
    source = {
        'pages': {'arp': {'type': 'arpeggiator'}},
        'page_settings': {'arpeggiator': settings},
    }
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    expected_settings = models.ArpeggiatorSettings.model_validate(settings).model_dump(
        mode='json', exclude_none=True
    )
    assert exported['page_settings']['arpeggiator'] == expected_settings
    assert compiler.compile_show(exported, {}, [], target) == data
    kind = str(selection['kind'])
    up, down, denominator = (2, 1, 8) if kind == 'walk' else (1, 1, 2)
    expected = arpeggiator.CONFIG.pack(
        b'CARP',
        1,
        kind == 'played',
        expected_kind,
        {'alternating': 3, 'walk': 4, 'played': 8}.get(kind, 0),
        65535,
        1,
        1,
        73,
        5 if kind == 'mod' else 2,
        1610612736,
        1006632960,
        up,
        down,
        denominator,
        8675309,
    )
    image, profile = tmp_path / 'setup.bin', tmp_path / 'expected.carp'
    image.write_bytes(data)
    profile.write_bytes(expected)
    native_tests.run_native([str(document_receiver), str(image), str(profile)])


@pytest.mark.parametrize(
    'value',
    [
        {'gate': '0'},
        {'gate': '2'},
        {'rhythm': {'step': '0 beat'}},
        {'selection': {'kind': 'walk', 'prob_up': '3/4', 'prob_down': '1/2'}},
        {'selection': {'kind': 'mod', 'modulus': 1}},
        {'selection': {'kind': 'choice', 'cycle': True}},
    ],
)
def test_invalid_profiles_are_rejected(value: dict[str, object]) -> None:
    with pytest.raises(ValueError):
        models.ArpeggiatorSettings.model_validate(value)


def test_a_page_supplies_defaults_and_a_bounded_budget(target: models.Target) -> None:
    data = compiler.compile_show(
        {'pages': {'arp': {'type': 'arpeggiator'}}}, {}, [], target
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    settings = exported['page_settings']['arpeggiator']
    assert settings['mode'] == 'poly' and settings['events'] == 128
