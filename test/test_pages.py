"""Named pages share one configuration and preserve order and window layouts."""

from pathlib import Path
from unittest.mock import DEFAULT, Mock

import pytest
import tomlkit

from cntrl.patch_chooser import (
    compiler,
    compiler_errors,
    compiler_inputs,
    layout,
    models,
    resolved,
)
from scripts import native_tests


def target() -> models.Target:
    return models.Target.model_validate(
        compiler_inputs.read_document(ROOT / 'target.json')
    )


@pytest.mark.parametrize(
    'pages',
    [
        {},
        {'live': {}},
        {'status': {'type': 'status'}},
        {'setups': {'type': 'setups'}},
        {'songs': {'type': 'songs-and-setlists'}},
        {'switches': {'type': 'switches'}},
        {'live': {}, 'status': {'type': 'status'}},
    ],
)
def test_page_configurations_round_trip(pages: dict[str, object]) -> None:
    source = {'pages': pages, 'fill_in_styles': False}
    data = compiler.compile_show(source, {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert list(exported['pages']) == list(pages)
    assert compiler.compile_show(exported, {}, [], target()) == data


def test_export_falls_back_only_for_known_authored_layout_errors(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    source = {'pages': {'live': {}, 'status': {'type': 'status'}}}
    data = compiler.compile_show(source, {}, [], target())
    pack = Mock(
        wraps=layout.pack,
        side_effect=[DEFAULT, layout.LayoutError('unsupported retained layout')],
    )
    monkeypatch.setattr(layout, 'pack', pack)
    assert list(tomlkit.parse(resolved.export(data))['pages']) == ['live', 'status']
    assert pack.call_count == 2

    failure = ValueError('unexpected layout failure')
    pack.side_effect = [DEFAULT, failure]
    with pytest.raises(ValueError) as caught:
        resolved.export(data)
    assert caught.value is failure


def test_pong_reserves_three_windows_and_round_trips() -> None:
    source = {'pages': {'game': {'type': 'pong'}}, 'fill_in_styles': False}
    data = compiler.compile_show(source, {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['pages']['game']['min_windows'] == 3
    assert compiler.compile_show(exported, {}, [], target()) == data


@pytest.mark.parametrize('kind', ['language', 'time'])
@pytest.mark.parametrize('count', [2, 3])
def test_two_window_page_reserves_two_windows_and_round_trips(
    count: int, kind: str
) -> None:
    device = target().model_copy(update={'displays': target().displays[:count]})
    source = {'pages': {'languages': {'type': kind}}, 'fill_in_styles': False}
    data = compiler.compile_show(source, {}, [], device)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['pages']['languages']['min_windows'] == 2
    assert compiler.compile_show(exported, {}, [], device) == data


@pytest.mark.parametrize('kind', ['language', 'time'])
def test_two_window_page_requires_two_displays(kind: str) -> None:
    device = target().model_copy(update={'displays': target().displays[:1]})
    with pytest.raises(compiler_errors.CompilationError, match='target display count'):
        compiler.compile_show({'pages': {'languages': {'type': kind}}}, {}, [], device)


def test_stored_standalone_tap_tempo_requires_recompilation() -> None:
    data = bytearray(
        layout.pack(
            models.Setup(pages={'tap': models.Page(type='time')}), ['left', 'right']
        )
    )
    data[12 + 2 * 32 + 32] = 0
    with pytest.raises(ValueError, match='Tap Tempo page.*recompile.*Time'):
        layout.unpack(bytes(data), 0)


@pytest.mark.parametrize('count', [1, 2, 3])
def test_default_tools_include_only_pages_that_fit_the_displays(
    count: int,
) -> None:
    device = target().model_copy(update={'displays': target().displays[:count]})
    data = compiler.compile_show({'fill_in_styles': False}, {}, [], device)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    pages = exported['pages']
    assert exported['layout']['focus'] == ('tools' if count >= 2 else 'main')
    assert not {'menu', 'trs-ports', 'expression'} & pages.keys()
    assert pages['patch-chooser']['min_windows'] == 0
    rotation = exported['regions']['tools' if count >= 2 else 'main']['pages']
    start = rotation.index('panic') + 1
    assert rotation[start : start + 3] == [
        'midi-monitor',
        'midi-looper',
        'arpeggiator',
    ]
    assert {'looper', 'arpeggiator'} <= exported['page_settings'].keys()
    if count == 3:
        assert exported['regions']['tools']['pages'][-5:] == [
            'setups',
            'songs-and-setlists',
            'switches',
            'calibration',
            'None',
        ]
        assert pages['pong']['min_windows'] == 3
    else:
        assert 'pong' not in pages
        assert 'None' in pages
    assert ('language' in pages) == (count >= 2)
    assert ('time' in pages) == (count >= 2)
    assert pages['calibration']['type'] == 'calibrate'
    assert pages['calibration']['min_windows'] == 1
    if count >= 2:
        assert pages['language']['min_windows'] == 2
    assert compiler.compile_show(exported, {}, [], device) == data


def test_doom_is_before_the_empty_page_in_the_default_tools_rotation() -> None:
    standard = target()
    device = standard.model_copy(update={'functions': [*standard.functions, 'doom']})
    data = compiler.compile_show({'fill_in_styles': False}, {}, [], device)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['regions']['tools']['pages'][-3:] == ['calibration', 'doom', 'None']
    assert exported['pages']['doom']['type'] == 'doom'
    assert exported['pages']['doom']['min_windows'] == 1
    assert compiler.compile_show(exported, {}, [], device) == data
    with pytest.raises(compiler_errors.CompilationError, match='Doom is unavailable'):
        compiler.compile_show(exported, {}, [], standard)


def test_insert_doom_preserves_the_compiled_data_outside_the_layout() -> None:
    original = compiler.compile_show({'fill_in_styles': False}, {}, [], target())
    updated = resolved.insert_doom_page(original, 'tools', 'None')
    start = original.index(b'CPAG')
    assert updated[28:start] == original[28:start]
    original_end = layout.section_end(original, start)
    updated_end = layout.section_end(updated, start)
    assert updated[updated_end:] == original[original_end:]
    old = layout.unpack(original, start)
    new = layout.unpack(updated, start)
    assert list(new['pages']) == [*old['pages'], 'doom']
    cycle = old['regions']['tools']['pages']
    cycle.insert(cycle.index('None'), 'doom')
    assert new['regions'] == old['regions']
    assert resolved.insert_doom_page(updated, 'tools', 'None') == updated


@pytest.mark.parametrize('kind', ['trs-ports', 'expression', 'menu'])
def test_removed_utility_pages_cannot_be_authored(kind: str) -> None:
    with pytest.raises(compiler_errors.CompilationError, match='type'):
        compiler.compile_show({'pages': {'removed': {'type': kind}}}, {}, [], target())


@pytest.mark.parametrize('kind', [5, 6])
def test_saved_removed_utility_pages_require_recompilation(kind: int) -> None:
    data = bytearray(
        layout.pack(models.Setup(pages={'tool': models.Page(type='status')}), ['lcd'])
    )
    data[12 + 32 + 32] = kind
    with pytest.raises(
        ValueError, match='TRS Ports and Expression pages have been removed'
    ):
        layout.unpack(bytes(data), 0)


@pytest.mark.parametrize('authored', [['status', 'time'], ['blank', 'time', 'status']])
def test_authored_region_lists_preserve_empty_stops(authored: list[str]) -> None:
    page_types = {'blank': 'None', 'status': 'status', 'time': 'time'}
    source = {
        'pages': {n: {'type': page_types[n]} for n in authored},
        'regions': {'main': {'pages': authored}},
    }
    data = compiler.compile_show(source, {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['regions']['main']['pages'] == authored
    assert list(exported['pages']) == authored


@pytest.mark.parametrize('authored', [['status', 'time'], ['status', 'None', 'time']])
def test_authored_regions_can_select_from_default_pages(authored: list[str]) -> None:
    source = {'regions': {'main': {'pages': authored}}}
    data = compiler.compile_show(source, {}, [], target())
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['regions']['main']['pages'] == authored
    assert set(exported['pages']) == set(authored)


def test_default_tools_honor_explicit_performance_focus() -> None:
    data = compiler.compile_show(
        {'layout': {'focus': 'performance'}, 'fill_in_styles': False}, {}, [], target()
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['layout']['focus'] == 'performance'


def test_explicit_pong_needs_three_displays() -> None:
    device = target().model_copy(update={'displays': target().displays[:2]})
    with pytest.raises(compiler_errors.CompilationError, match='target display count'):
        compiler.compile_show({'pages': {'game': {'type': 'pong'}}}, {}, [], device)


def test_named_instances_and_order_survive_compilation() -> None:
    source = compiler_inputs.read_document(ROOT / 'pages.toml')
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


def test_migrated_legacy_setup_keeps_live_patch_state_while_cycling_tools(
    tmp_path: Path,
    native_programs: native_tests.NativePrograms,
    legacy_setups: dict[int, bytes],
) -> None:
    original = legacy_setups[36]
    legacy = models.Setup.model_validate(
        layout.unpack(original, original.index(b'CPAG'))
    )
    assert sum(p.type == 'patch-chooser' for p in legacy.pages.values()) == 1
    migrated = tmp_path / 'legacy.golden'
    migrated.write_text(resolved.upgrade(original).hex())
    program = native_programs.build('test/test_application.cpp')
    native_tests.run_native([str(program), str(ROOT / 'pages.golden'), str(migrated)])


@pytest.mark.parametrize(
    'source',
    [
        {'apps': {}},
        {'pages': {'x': {'give_up_windows': ['right', 'right']}}},
        {'pages': {'x': {'type': 'unknown'}}},
        {'pages': {'x': {'type': 'tap-tempo'}}},
        {'pages': {'x': {'type': 'clock-tempo'}}},
        {'pages': {'x': {'type': 'clock-transport'}}},
        {'pages': {'x': {'type': 'firmware'}}},
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
    data = layout.pack(
        models.Setup(pages={'x': models.Page(type='status', min_windows=2)}), windows
    )
    assert layout.unpack(data, 0)['pages']['x']['min_windows'] == 1


def test_window_priority_requires_every_declared_id() -> None:
    with pytest.raises(ValueError, match='every target display ID'):
        layout.pack(
            models.Setup(pages={'x': models.Page(give_up_windows=['right'])}),
            ['left', 'center', 'right'],
        )


@pytest.mark.parametrize('count', [5, 40, 255])
def test_patch_chooser_instances_follow_encoded_page_count(count: int) -> None:
    data = layout.pack(
        models.Setup(pages={str(i): models.Page() for i in range(count)}), ['screen']
    )
    assert len(layout.unpack(data, 0)['pages']) == count


@pytest.mark.parametrize(
    'kind',
    [
        t
        for t in layout.PAGE_TYPES
        if t
        and t not in ('None', 'patch-chooser', 'banked', 'pong', 'language', 'time')
    ],
)
@pytest.mark.parametrize('count', [0, 1, 3])
def test_tool_pages_always_compile_for_one_window(kind: str, count: int) -> None:
    device = target()
    if kind == 'doom':
        device = device.model_copy(update={'functions': [*device.functions, 'doom']})
    data = compiler.compile_show(
        {'pages': {'tool': {'type': kind, 'min_windows': count}}},
        {},
        [],
        device,
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['pages']['tool']['type'] == kind
    assert exported['pages']['tool']['min_windows'] == 1
    assert compiler.compile_show(exported, {}, [], device) == data


@pytest.mark.parametrize('count', [0, 3])
def test_saved_tool_window_counts_export_as_one_window(count: int) -> None:
    data = bytearray(
        layout.pack(
            models.Setup(pages={'tap': models.Page(type='status')}),
            ['left', 'center', 'right'],
        )
    )
    data[12 + 3 * 32 + 33] = count
    exported = layout.unpack(bytes(data), 0)
    assert exported['pages']['tap']['min_windows'] == 1


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


def test_firmware_and_authoring_agree_on_page_capabilities(
    native_programs: native_tests.NativePrograms,
) -> None:
    program = native_programs.build('test/test_page_layout.cpp')
    result = native_tests.run_native(
        [str(program), 'caps'], capture_output=True, text=True
    )
    assert [[int(n) for n in r.split()] for r in result.stdout.splitlines()] == [
        [models.Page(type=t).maximum_windows(n) for n in [1, 3, 5]]
        for t in layout.PAGE_TYPES
        if t
    ]


ROOT = Path(__file__).parents[1] / 'firmware/test'
