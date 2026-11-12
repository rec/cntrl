"""Named pages share one configuration and preserve order and window layouts."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, layout, models, resolved
from scripts import native_tests


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
        {'pages': {'x': {'type': 'None', 'min_windows': 1}}},
        {'pages': {'x': {'min_windows': -1}}},
        {'pages': {'x': {'windows': 1}}},
        {'layout': {'left_wins': 1}},
        {'layout': {'left_wins': 'false'}},
    ],
)
def test_invalid_page_configurations_are_rejected(source: dict[str, object]) -> None:
    with pytest.raises(ValueError):
        models.Setup.model_validate(source)


@pytest.mark.parametrize('count', [1, 2, 5])
def test_layout_follows_declared_displays(count: int) -> None:
    windows = [f'window-{i}' for i in range(count)]
    pages = {'live': models.Page(), 'status': models.Page(type='status')}
    data = layout.pack(models.Setup(pages=pages), windows)
    assert layout.section_end(data, 0) == len(data)
    restored = layout.unpack(data, 0)['pages']
    assert restored['live']['give_up_windows'] == list(reversed(windows))
    assert restored['live']['min_windows'] == 1
    assert restored['status']['min_windows'] == 1
    with pytest.raises(ValueError, match='maximum'):
        layout.pack(
            models.Setup(pages={'x': models.Page(type='status', min_windows=2)}),
            windows,
        )


def test_window_priority_requires_every_declared_id() -> None:
    with pytest.raises(ValueError, match='every target display ID'):
        layout.pack(
            models.Setup(pages={'x': models.Page(give_up_windows=['right'])}),
            ['left', 'center', 'right'],
        )


@pytest.mark.parametrize('count', [5, 40, 255])
def test_patch_switcher_instances_follow_encoded_page_count(count: int) -> None:
    data = layout.pack(
        models.Setup(pages={str(i): models.Page() for i in range(count)}), ['screen']
    )
    assert len(layout.unpack(data, 0)['pages']) == count


@pytest.mark.parametrize('count', [1, 2, 3])
def test_midi_monitor_layout_round_trips(count: int) -> None:
    data = compiler.compile_show(
        {'pages': {'monitor': {'type': 'midi-monitor', 'min_windows': count}}},
        {},
        [],
        target(),
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['pages']['monitor']['type'] == 'midi-monitor'
    assert exported['pages']['monitor']['min_windows'] == count
    assert compiler.compile_show(exported, {}, [], target()) == data


def test_target_aware_defaults_and_initial_focus() -> None:
    show = models.Setup(
        pages={'status': models.Page(type='status'), 'live': models.Page()}
    )
    for windows, order in [
        (['one'], ['main']),
        (['one', 'two', 'three'], ['performance', 'tools']),
    ]:
        data = layout.pack(show, windows)
        exported = layout.unpack(data, 0)
        assert exported['layout'] == {
            'order': order,
            'focus': order[0],
            'left_wins': False,
        }
        assert layout.pack(models.Setup.model_validate(exported), windows) == data


@pytest.mark.parametrize('left_wins', [False, True])
@pytest.mark.parametrize('order', [['left', 'right'], ['right', 'left']])
def test_explicit_regions_preserve_cycles_placement_and_priority(
    left_wins: bool, order: list[str]
) -> None:
    show = models.Setup.model_validate(
        {
            'pages': {
                'monitor': {'type': 'midi-monitor', 'min_windows': 0},
                'live': {},
                'blank': {'type': 'None'},
            },
            'regions': {
                'left': {'pages': ['monitor', 'blank']},
                'right': {'pages': ['live']},
            },
            'layout': {'order': order, 'left_wins': left_wins},
        }
    )
    data = layout.pack(show, ['one', 'two', 'three'])
    exported = layout.unpack(data, 0)
    assert exported['layout'] == {
        'order': order,
        'left_wins': left_wins,
        'focus': order[0],
    }
    assert exported['regions'] == {
        'left': {'pages': ['monitor', 'blank']},
        'right': {'pages': ['live']},
    }
    assert (
        layout.pack(models.Setup.model_validate(exported), ['one', 'two', 'three'])
        == data
    )


@pytest.mark.parametrize(
    'regions,settings',
    [
        ({'a': {'pages': ['live']}, 'b': {'pages': ['live']}}, {}),
        ({'a': {'pages': ['missing', 'live']}}, {}),
        ({'a': {'pages': ['live', 'status']}}, {'order': ['wrong']}),
        ({'a': {'pages': ['live', 'status']}}, {'order': ['a', 'a']}),
        ({'a': {'pages': ['live', 'status']}}, {'focus': 'wrong'}),
    ],
)
def test_invalid_region_membership_and_placement_are_rejected(
    regions: dict[str, object], settings: dict[str, object]
) -> None:
    show = models.Setup.model_validate(
        {
            'pages': {'live': {}, 'status': {'type': 'status'}},
            'regions': regions,
            'layout': settings,
        }
    )
    with pytest.raises(ValueError):
        layout.pack(show, ['one', 'two', 'three'])


def test_later_selection_cannot_overcommit_minimums() -> None:
    show = models.Setup.model_validate(
        {
            'pages': {
                'small': {},
                'large': {'min_windows': 3},
                'tool': {'type': 'status'},
            },
            'regions': {
                'live': {'pages': ['small', 'large']},
                'tools': {'pages': ['tool']},
            },
        }
    )
    with pytest.raises(ValueError, match='live/large=3, tools/tool=1'):
        layout.pack(show, ['one', 'two', 'three'])


def test_firmware_and_authoring_agree_on_page_capabilities(tmp_path: Path) -> None:
    program = tmp_path / 'page-layout'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            f'-I{ROOT.parent / "include"}',
            str(ROOT / 'test_page_layout.cpp'),
            '-o',
            str(program),
        ]
    )
    result = native_tests.run_native(
        [str(program), 'caps'], capture_output=True, text=True
    )
    assert [[int(n) for n in r.split()] for r in result.stdout.splitlines()] == [
        [models.Page(type=t).maximum_windows(n) for n in [1, 3, 5]]
        for t in layout.PAGE_TYPES
    ]


ROOT = Path(__file__).parents[1] / 'firmware/test'
