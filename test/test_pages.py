"""Named pages share one configuration and preserve order and window layouts."""

import struct
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, layout, models, resolved


def target() -> models.Target:
    return models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))


@pytest.mark.parametrize(
    'pages',
    [
        {},
        {'live': {}},
        {'status': {'type': 'status'}},
        {'live': {}, 'status': {'type': 'status'}},
    ],
)
def test_page_configurations_round_trip(pages: dict[str, object]) -> None:
    source = {'pages': pages, 'fill_in_styles': False}
    data = compiler.compile_show(source, {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert list(exported['pages']) == list(pages)
    assert compiler.compile_show(exported, {}, [], target()) == data


def test_named_instances_and_order_survive_compilation() -> None:
    source = compiler.read_document(ROOT / 'pages.toml')
    data = compiler.compile_show(source, {}, [], target())
    assert data.hex() == (ROOT / 'pages.golden').read_text().strip()
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert list(exported['pages']) == [
        'full',
        'status',
        'wide',
        'all',
        'tap',
        'lead',
        'rhythm',
    ]
    assert exported['pages']['rhythm']['give_up_windows'] == ['left', 'right', 'center']
    assert compiler.compile_show(exported, {}, [], target()) == data


def test_compiled_ram_budget_counts_only_declared_switcher_instances() -> None:
    def compile_pages(pages: dict[str, object]) -> tuple[int, int, int]:
        data = compiler.compile_show({'pages': pages}, {}, [], target())
        instruments, cycles, patches = struct.unpack_from('<HHH', data, 12)
        metadata = 76 + instruments * 68 + cycles * 68 + patches * 82
        return compiler.METADATA.unpack_from(data, metadata)[-2], instruments, cycles

    single, instruments, cycles = compile_pages({'live': {}})
    double, _, _ = compile_pages({'live': {}, 'second': {}})
    status, _, _ = compile_pages({'live': {}, 'status': {'type': 'status'}})
    assert (instruments, cycles) == (0, 0)
    assert double - single == 376
    assert status - single == 8


@pytest.mark.parametrize(
    'source',
    [
        {'apps': {}},
        {'pages': {'x': {'give_up_windows': ['right', 'right']}}},
        {'pages': {'x': {'type': 'unknown'}}},
        {'pages': {'x': {'type': 'None', 'windows': 1}}},
        {'pages': {'x': {'windows': -1}}},
    ],
)
def test_invalid_page_configurations_are_rejected(source: dict[str, object]) -> None:
    with pytest.raises(ValueError):
        models.Setup.model_validate(source)


@pytest.mark.parametrize('count', [1, 2, 5])
def test_layout_follows_declared_displays(count: int) -> None:
    windows = [f'window-{i}' for i in range(count)]
    pages = {'live': models.Page(), 'status': models.Page(type='status', windows=count)}
    data = layout.pack(pages, windows)
    assert layout.section_end(data, 0) == len(data)
    restored = layout.unpack(data, 0)['pages']
    assert restored['live']['give_up_windows'] == list(reversed(windows))
    assert restored['live']['windows'] == count
    assert restored['status']['windows'] == count
    with pytest.raises(ValueError, match='more windows'):
        layout.pack({'x': models.Page(type='status', windows=count + 1)}, windows)


def test_window_priority_requires_every_declared_id() -> None:
    with pytest.raises(ValueError, match='every target display ID'):
        layout.pack(
            {'x': models.Page(give_up_windows=['right'])}, ['left', 'center', 'right']
        )


@pytest.mark.parametrize('count', [5, 40, 255])
def test_patch_switcher_instances_follow_encoded_page_count(count: int) -> None:
    pages = {str(i): models.Page() for i in range(count)}
    data = layout.pack(pages, ['screen'])
    assert len(layout.unpack(data, 0)['pages']) == count


@pytest.mark.parametrize('count', [1, 2, 3])
def test_midi_monitor_layout_round_trips(count: int) -> None:
    data = compiler.compile_show(
        {'pages': {'monitor': {'type': 'midi-monitor', 'windows': count}}},
        {},
        [],
        target(),
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['pages']['monitor']['type'] == 'midi-monitor'
    assert exported['pages']['monitor']['windows'] == count
    assert compiler.compile_show(exported, {}, [], target()) == data


def test_midi_monitor_defaults_to_two_windows_and_supports_single_display() -> None:
    data = layout.pack(
        {'monitor': models.Page(type='midi-monitor')}, ['one', 'two', 'three']
    )
    assert layout.unpack(data, 0)['pages']['monitor']['windows'] == 2
    data = layout.pack(
        {'monitor': models.Page(type='midi-monitor', windows=1)}, ['one']
    )
    assert layout.unpack(data, 0)['pages']['monitor']['windows'] == 1
    with pytest.raises(ValueError, match='more windows'):
        layout.pack({'monitor': models.Page(type='midi-monitor')}, ['one'])
    with pytest.raises(ValueError, match='more windows'):
        layout.pack(
            {'monitor': models.Page(type='midi-monitor', windows=3)}, ['one', 'two']
        )


@pytest.mark.parametrize('count', [0, 4])
def test_invalid_midi_monitor_window_count_is_rejected(count: int) -> None:
    with pytest.raises(ValueError, match='one, two or three'):
        models.Page(type='midi-monitor', windows=count)


ROOT = Path(__file__).parents[1] / 'firmware/test'
