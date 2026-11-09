"""Named pages share one configuration and preserve order and window layouts."""

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


def test_patch_switcher_instance_limit_is_preserved() -> None:
    with pytest.raises(ValueError, match='four patch-switcher'):
        layout.pack({str(i): models.Page() for i in range(5)}, ['screen'])


ROOT = Path(__file__).parents[1] / 'firmware/test'
