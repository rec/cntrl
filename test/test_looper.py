"""Shared looper configuration, editable export and capacity accounting."""

import struct
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import (
    compiler,
    layout,
    looper,
    midi_processing,
    models,
    resolved,
)


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'firmware/test/target.json')
    )


def test_looper_views_share_one_budget_and_round_trip(target: models.Target) -> None:
    source = {
        'pages': {'loop': {'type': 'midi-looper'}},
        'page_settings': {
            'looper': {
                'inputs': ['din'],
                'outputs': ['din'],
                'channels': [1, 3],
                'events': 512,
            }
        },
        'fill_in_styles': False,
    }
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data
    settings = models.Setup.model_validate(exported).page_settings.looper
    assert (
        settings is not None and settings.channels == [1, 3] and settings.events == 512
    )
    config = looper.pack(models.Setup.model_validate(source), target)
    expected = (
        looper.estimate_ram(config)
        + layout.estimate_ram(
            models.Setup.model_validate(source).pages,
            0,
            0,
            switches=bool(target.max_switches),
            regions=1,
        )
        + midi_processing.estimate_ram(b'', endpoint_count=len(target.midi_endpoints))
    )
    assert resolved.arm_memory(data)['looper'] == looper.estimate_arm_ram(
        config, capture_contexts=2
    )
    instruments, cycles, patches = struct.unpack_from('<HHH', data, 12)
    metadata = 76 + instruments * 68 + cycles * 68 + patches * 82
    assert (
        struct.unpack_from('<I', data, metadata + compiler.METADATA.size - 8)[0]
        == expected
    )
    source['pages']['other'] = {'type': 'midi-looper'}
    second = compiler.compile_show(source, {}, [], target)
    assert second.count(b'CLOP') == 1
    additional_page = layout.estimate_ram(
        models.Setup.model_validate(source).pages,
        0,
        0,
        switches=bool(target.max_switches),
        regions=1,
    ) - layout.estimate_ram(
        models.Setup.model_validate(exported).pages,
        0,
        0,
        switches=bool(target.max_switches),
        regions=1,
    )
    assert (
        struct.unpack_from('<I', second, metadata + compiler.METADATA.size - 8)[0]
        == expected + additional_page
    )


@pytest.mark.parametrize('field', ['inputs', 'outputs'])
def test_unavailable_looper_endpoints_are_rejected(
    target: models.Target, field: str
) -> None:
    with pytest.raises(compiler.CompilationError, match='unavailable'):
        compiler.compile_show(
            {'page_settings': {'looper': {field: ['missing']}}}, {}, [], target
        )


def test_a_looper_page_gets_the_default_shared_configuration(
    target: models.Target,
) -> None:
    data = compiler.compile_show(
        {'pages': {'loop': {'type': 'midi-looper'}}}, {}, [], target
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    settings = models.Setup.model_validate(exported).page_settings.looper
    assert settings is not None and settings.events == 512


@pytest.mark.parametrize('events', [0, 65536, True])
def test_event_capacity_is_bounded(events: object) -> None:
    with pytest.raises(ValueError):
        models.LooperSettings.model_validate({'events': events})


def test_all_looper_actions_round_trip_with_and_without_a_view(
    target: models.Target,
) -> None:
    root = Path(__file__).parents[1] / 'firmware/test'
    source = compiler.read_document(root / 'looper.toml')
    assert isinstance(source, dict)
    data = compiler.compile_show(source, {}, [], target)
    assert data.hex() == (root / 'looper.golden').read_text().strip()
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data
    source['pages'] = {}
    data = compiler.compile_show(source, {}, [], target)
    assert (
        compiler.compile_show(
            tomlkit.parse(resolved.export(data)).unwrap(), {}, [], target
        )
        == data
    )
