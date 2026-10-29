"""Pages, field binding, style inheritance and editable display metadata."""

import struct
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, display, graphics, models, resolved


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(FIXTURES / 'target.json')
    )


def patch() -> display.DisplayPatch:
    return display.DisplayPatch(
        values={
            'type': 'program-change',
            'patch_list.name': 'wind',
            'display_name': 'Flute',
            'name': 'Flute',
            'program': 1,
            'bank': None,
            'instrument': 'a',
            'volume': None,
            'pan': None,
        }
    )


def lines(block: bytes, page: int) -> list[tuple[int, tuple[int, ...], int, str]]:
    _, count, _, texts, page_size, _ = display.HEADER.unpack_from(block)
    styles = [graphics.SCREEN.unpack_from(block, 16 + i * 16) for i in range(count)]
    position = 16 + count * 16
    text_position = position + page_size
    strings: list[str] = []
    for _ in range(texts):
        size = struct.unpack_from('<H', block, text_position)[0]
        strings.append(block[text_position + 2 : text_position + 1 + size].decode())
        text_position += 2 + size
    for _ in range(page):
        position += 1 + block[position] * 8
    return [
        (c, styles[s], x, strings[t])
        for c, _, s, x, t in (
            display.LINE.unpack_from(block, position + 1 + i * 8)
            for i in range(block[position])
        )
    ]


def test_default_page_reproduces_tag_and_patch_ui(target: models.Target) -> None:
    windows, references, block = display.compile_display(
        models.Show(instruments={}, patches={}), [patch()], target
    )
    expected = [
        (0, (0, 48, 4, 0, 0, 65535, 48, 0), 8, 'wind'),
        (1, (0, 40, 4, 0, 0, 65535, 40, 0), 5, 'Flute'),
    ]
    assert all(lines(block, r) == expected for r in references[0])
    assert all(graphics.SCREEN.unpack(w)[1:3] == (8, 4) for w in windows)
    assert all(w[12:] == bytes(4) for w in windows)


def test_display_fixture_matches_portable_firmware(target: models.Target) -> None:
    data = compiler.compile_show(
        compiler.read_document(FIXTURES / 'display.toml'),
        compiler.read_document(FIXTURES / 'definitions.json'),
        [],
        target,
    )
    assert data.hex() == (FIXTURES / 'display.golden').read_text().strip()


def test_a_window_can_select_a_different_page(target: models.Target) -> None:
    show = models.Show.model_validate(
        {
            'instruments': {},
            'patch_lists': {},
            'pages': {
                'patch': {'fields': ['display_name']},
                'tag': {'fields': ['patch_list.name']},
            },
            'windows': {'sent': {'page': 'tag', 'pad': 12}},
        }
    )
    windows, refs, block = display.compile_display(show, [patch()], target)
    assert [x[3] for x in lines(block, refs[0][0])] == ['Flute']
    assert [x[3] for x in lines(block, refs[0][1])] == ['wind']
    assert graphics.SCREEN.unpack(windows[1])[2] == 12


def test_each_line_autoscales_independently(target: models.Target) -> None:
    value = patch().model_copy(
        update={
            'values': patch().values
            | {'patch_list.name': 'A', 'display_name': 'W' * 11}
        }
    )
    _, refs, block = display.compile_display(
        models.Show(instruments={}, patches={}),
        [value],
        target,
    )
    first, second = lines(block, refs[0][0])
    assert (first[1][1], first[2]) == (56, 59)
    assert (second[1][1], second[2]) == (16, 14)


def test_styles_merge_per_property_and_patch_list_heading_excludes_content(
    target: models.Target,
) -> None:
    value = patch().model_copy(
        update={
            'program_style': models.Style(foreground_color='red'),
            'style': models.Style(foreground_color='green'),
            'tags': ['wind'],
        }
    )
    show = models.Show.model_validate(
        {
            'instruments': {},
            'patch_lists': {'wind': []},
            'windows': {'next': {'style': {'background_color': 'blue'}}},
            'pages': {
                'patch': {
                    'style': {'foreground_color': 'yellow'},
                    'line_styles': {'display_name': {'foreground_color': 'green'}},
                }
            },
            'tags': {'wind': {'style': {'foreground_color': 'white'}}},
        }
    )
    _, refs, block = display.compile_display(show, [value], target)
    heading, label = lines(block, refs[0][2])
    assert (heading[1][4], heading[1][5]) == (65504, 31)
    assert (label[1][4], label[1][5]) == (1024, 31)


def test_page_fields_use_window_data_and_global_parameters(
    target: models.Target,
) -> None:
    show = models.Show.model_validate(
        {
            'instruments': {},
            'patch_lists': {},
            'parameters': {
                'tempo': {'value': 120},
                'off': {'value': False},
                'zero': {'value': 0},
                'missing': {},
            },
            'pages': {
                'patch': {'fields': ['display_name', 'tempo', 'missing', 'off', 'zero']}
            },
        }
    )
    other = patch().model_copy(
        update={'values': patch().values | {'display_name': 'Sax'}}
    )
    _, refs, block = display.compile_display(show, [patch(), other], target)
    assert [x[3] for x in lines(block, refs[0][0])] == ['Flute', '120', 'false', '0']
    assert [x[3] for x in lines(block, refs[1][0])] == ['Sax', '120', 'false', '0']


@pytest.mark.parametrize(
    'config,message',
    [
        ({'parameters': {'program': {'value': 1}}}, 'duplicate field'),
        ({'pages': {'patch': {'fields': ['unknown']}}}, 'unknown fields'),
        (
            {'pages': {'patch': {'fields': ['program', 'program']}}},
            'duplicate page field',
        ),
        ({'windows': {'sent': {'page': 'absent'}}}, 'unknown page'),
        ({'pages': {'patch': {'line_styles': {'missing': {}}}}}, 'line_styles'),
    ],
)
def test_ambiguous_or_unknown_references_are_rejected(
    config: dict[str, object], message: str, target: models.Target
) -> None:
    with pytest.raises(ValueError, match=message):
        show = models.Show.model_validate(
            {'instruments': {}, 'patch_lists': {}} | config
        )
        display.compile_display(show, [patch()], target)


def test_window_errors_are_reported_together(target: models.Target) -> None:
    show = models.Show.model_validate(
        {
            'instruments': {},
            'patch_lists': {},
            'windows': {
                n: {'style': {'foreground_color': 'unknown'}}
                for n in display.WINDOW_NAMES
            },
        }
    )
    with pytest.raises(ValueError) as failure:
        display.compile_display(show, [patch()], target)
    assert all(f'windows.{n}' in str(failure.value) for n in display.WINDOW_NAMES)


def test_export_preserves_styles_and_unset_properties(target: models.Target) -> None:
    show = compiler.read_document(FIXTURES / 'show.json')
    assert isinstance(show, dict)
    show['contrast'] = 0.5
    show['pages'] = {
        'patch': {
            'fields': ['patch_list.name', 'display_name', 'brightness'],
            'line_styles': {'brightness': {'foreground_color': 'blue'}},
        }
    }
    show['tags'] = {'wind': {'style': {'foreground_color': 'red'}}}
    show['windows'] = {'next': {'pad': '8 px', 'style': {'background_color': 'black'}}}
    show['patch_lists']['wind'][0]['style'] = {'background_color': 'white'}
    definitions = compiler.read_document(FIXTURES / 'definitions.json')
    assert isinstance(definitions, dict)
    definitions['programs'] = [
        {
            'instrument': 'synth',
            'setup': 'stock',
            'program': 1,
            'bank': 0,
            'name': 'Flute',
            'style': {'font_name': 'classic', 'foreground_color': 'blue'},
        }
    ]
    data = compiler.compile_show(show, definitions, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert 'font_size' not in exported and 'font_name' not in exported
    assert exported['patch_lists']['wind'][0]['style'] == {'background_color': 'white'}
    assert (
        exported['definitions']['programs'][0]['style']
        == definitions['programs'][0]['style']
    )
    assert (
        exported['pages']['patch']['line_styles']['brightness']['foreground_color']
        == 'blue'
    )
    assert compiler.compile_show(exported, {}, [], target) == data


def test_tag_labels_and_patch_content_resolve_their_own_styles(
    target: models.Target,
) -> None:
    show = models.Show(
        instruments={},
        patches={},
        tags={
            'sax': models.Tag(style=models.Style(foreground_color='red')),
            'whole-system': models.Tag(style=models.Style(foreground_color='blue')),
        },
        pages={'patch': models.Page(fields=['tags', 'display_name'])},
    )
    value = patch().model_copy(
        update={
            'program_tags': ['sax'],
            'tags': ['whole-system'],
            'program_style': models.Style(font_name='classic'),
            'style': models.Style(foreground_color='green'),
        }
    )
    _, refs, block = display.compile_display(show, [value], target)
    tags, label = lines(block, refs[0][2])
    assert tags[3] == 'sax, whole-system'
    assert tags[1][4] == 31
    assert label[1][4] == 1024


FIXTURES = Path(__file__).resolve().parents[1] / 'firmware/test'


def test_local_tag_reapplies_after_program_tags(
    target: models.Target,
) -> None:
    show = models.Show(
        instruments={},
        patches={},
        style=models.Style(background_color='white'),
        tags={
            'sax': models.Tag(style=models.Style(foreground_color='red')),
            'bright': models.Tag(style=models.Style(foreground_color='blue')),
        },
        pages={'patch': models.Page(fields=['tags', 'display_name'])},
    )
    value = patch().model_copy(
        update={'program_tags': ['sax', 'bright'], 'tags': ['sax']}
    )
    _, refs, block = display.compile_display(show, [value], target)
    heading, label = lines(block, refs[0][0])
    assert heading[3] == 'sax, bright'
    assert heading[1][4] == label[1][4] == graphics.rgb565((255, 0, 0))
