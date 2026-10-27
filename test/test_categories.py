"""Reusable categories, inheritance, style priority and authored round trips."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, display, models, resolved


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'firmware/test/target.json')
    )


@pytest.fixture
def show() -> dict[str, object]:
    return models.Show(
        instruments={
            'synth': {
                'instrument': 'synth',
                'setup': 'default',
                'channel': 1,
                'output': 'din',
            }
        },
        definitions=models.Definitions(
            setups=[models.Setup(instrument='synth', name='default')],
            programs=[
                models.TablePatch(
                    instrument='synth',
                    setup='default',
                    program=1,
                    name='Sax',
                    categories=['sax'],
                ),
            ],
        ),
        patches={
            'Song 1': [
                {'instrument': 'synth', 'program': 1, 'categories': ['whole-system']},
                {'instrument': 'synth', 'program': 1},
            ]
        },
        categories={'sax': models.Category(), 'whole-system': models.Category()},
        pages={
            'patch': models.Page(fields=['group.name', 'categories', 'display_name'])
        },
        font_size=8,
    ).model_dump(exclude_none=True)


def test_patch_inherits_program_categories_without_changing_groups(
    show: dict[str, object], target: models.Target
) -> None:
    data = compiler.compile_show(show, {}, [], target)
    assert b'sax, whole-system\0' in data
    assert b'sax\0' in data
    exported = models.Show.model_validate(tomlkit.parse(resolved.export(data)).unwrap())
    assert list(exported.patches) == ['Song 1']
    assert exported.definitions is not None
    assert exported.definitions.programs[0].categories == ['sax']
    raw = exported.patches['Song 1']
    assert isinstance(raw, list)
    patches = [models.Patch.model_validate(p) for p in raw]
    assert [p.categories for p in patches] == [['whole-system'], []]
    assert (
        compiler.compile_show(exported.model_dump(exclude_none=True), {}, [], target)
        == data
    )
    changed = exported.model_dump(exclude_none=True)
    changed['definitions']['programs'][0]['categories'] = []
    rebuilt = compiler.compile_show(changed, {}, [], target)
    assert b'sax, whole-system\0' not in rebuilt
    assert b'whole-system\0' in rebuilt


def test_categories_are_inherited_once_in_child_then_parent_order() -> None:
    patch = display.DisplayPatch(
        values={},
        program_categories=['sax', 'wind'],
        categories=['sax', 'whole-system'],
    )
    assert patch.effective_categories == ['sax', 'wind', 'whole-system']


def test_category_priority_resolves_each_property_independently() -> None:
    categories = {
        'sax': models.Category(
            style=models.Style(
                foreground_color='red', font_size=16, autoscale_fonts=True
            )
        ),
        'whole-system': models.Category(
            priority=1,
            style=models.Style(foreground_color='blue', autoscale_fonts=False),
        ),
    }
    expected = models.Style(
        foreground_color='blue', font_size=16, autoscale_fonts=False
    )
    assert display.category_style(['sax', 'whole-system'], categories) == expected
    assert display.category_style(['whole-system', 'sax'], categories) == expected


def test_equal_priority_categories_can_combine_or_agree() -> None:
    categories = {
        'sax': models.Category(style=models.Style(foreground_color='red')),
        'whole-system': models.Category(
            style=models.Style(foreground_color='0xFF0000', font_size=16)
        ),
    }
    assert display.category_style(list(categories), categories) == models.Style(
        foreground_color='red', font_size=16
    )


def test_conflicting_winning_category_priorities_are_rejected(
    show: dict[str, object], target: models.Target
) -> None:
    show['categories'] = {
        'sax': {'style': {'foreground_color': 'red'}},
        'whole-system': {'style': {'foreground_color': 'blue'}},
    }
    with pytest.raises(
        compiler.CompilationError, match='conflicting foreground_color at priority 0'
    ):
        compiler.compile_show(show, {}, [], target)


@pytest.mark.parametrize('owner', ['program', 'patch'])
def test_unknown_category_references_are_errors(
    owner: str, show: dict[str, object], target: models.Target
) -> None:
    document = models.Show.model_validate(show).model_dump(exclude_none=True)
    entry = (
        document['definitions']['programs'][0]
        if owner == 'program'
        else document['patches']['Song 1'][0]
    )
    entry['categories'] = ['absent']
    with pytest.raises(compiler.CompilationError, match='unknown categories.*absent'):
        compiler.compile_show(document, {}, [], target)


def test_program_table_categories_merge_as_a_union(
    show: dict[str, object], target: models.Target
) -> None:
    table = {
        'patches': [
            {
                'instrument': 'synth',
                'setup': 'default',
                'program': 1,
                'categories': ['whole-system', 'sax'],
            }
        ]
    }
    data = compiler.compile_show(show, {}, [table], target)
    exported = models.Show.model_validate(tomlkit.parse(resolved.export(data)).unwrap())
    assert exported.definitions is not None
    assert exported.definitions.programs[0].categories == ['sax', 'whole-system']
