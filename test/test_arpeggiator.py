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


@pytest.mark.parametrize('mode', ['poly', 'mono'])
@pytest.mark.parametrize('kind', arpeggiator.KINDS)
def test_profiles_round_trip(mode: str, kind: str, target: models.Target) -> None:
    if (
        kind == 'played'
        and mode != 'mono'
        or kind in ('ascending', 'descending')
        and mode != 'poly'
    ):
        return
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
