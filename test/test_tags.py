"""Reusable tags, inheritance, ordered styles and authored round trips."""

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
                    tags=['sax'],
                ),
            ],
        ),
        patch_lists={
            'Song 1': [
                {
                    'type': 'program-change',
                    'instrument': 'synth',
                    'program': 1,
                    'tags': ['whole-system'],
                },
                {'type': 'program-change', 'instrument': 'synth', 'program': 1},
            ]
        },
        tags={'sax': models.Tag(), 'whole-system': models.Tag()},
        pages={
            'patch': models.Page(fields=['patch_list.name', 'tags', 'display_name'])
        },
    ).model_dump(exclude_none=True)


def test_patch_inherits_program_tags_without_changing_patch_lists(
    show: dict[str, object], target: models.Target
) -> None:
    data = compiler.compile_show(show, {}, [], target)
    assert b'sax, whole-system\0' in data
    assert b'sax\0' in data
    exported = models.Show.model_validate(tomlkit.parse(resolved.export(data)).unwrap())
    assert list(exported.patch_lists) == ['Song 1']
    assert exported.definitions is not None
    assert exported.definitions.programs[0].tags == ['sax']
    raw = exported.patch_lists['Song 1']
    assert isinstance(raw, list)
    patches = [models.ProgramChange.model_validate(p) for p in raw]
    assert [p.tags for p in patches] == [['whole-system'], []]
    assert (
        compiler.compile_show(exported.model_dump(exclude_none=True), {}, [], target)
        == data
    )
    changed = exported.model_dump(exclude_none=True)
    changed['definitions']['programs'][0]['tags'] = []
    rebuilt = compiler.compile_show(changed, {}, [], target)
    assert b'sax, whole-system\0' not in rebuilt
    assert b'whole-system\0' in rebuilt


def test_tags_are_inherited_once_in_child_then_parent_order() -> None:
    patch = display.DisplayPatch(
        values={},
        program_tags=['sax', 'wind'],
        tags=['sax', 'whole-system'],
    )
    assert patch.effective_tags == ['sax', 'wind', 'whole-system']


def test_later_tags_override_properties_without_replacing_the_style() -> None:
    tags = {
        'sax': models.Tag(
            style=models.Style(foreground_color='red', font_name='classic')
        ),
        'whole-system': models.Tag(style=models.Style(foreground_color='blue')),
    }
    assert display.tag_style(['sax', 'whole-system'], tags) == models.Style(
        foreground_color='blue', font_name='classic'
    )
    assert (
        display.tag_style(['sax', 'whole-system', 'sax'], tags).foreground_color
        == 'red'
    )


@pytest.mark.parametrize('owner', ['program', 'patch'])
def test_unstyled_tags_survive_without_declarations(
    owner: str, show: dict[str, object], target: models.Target
) -> None:
    document = models.Show.model_validate(show).model_dump(exclude_none=True)
    entry = (
        document['definitions']['programs'][0]
        if owner == 'program'
        else document['patch_lists']['Song 1'][0]
    )
    entry['tags'] = ['absent']
    data = compiler.compile_show(document, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    membership = (
        exported['definitions']['programs'][0]['tags']
        if owner == 'program'
        else exported['patch_lists']['Song 1'][0]['tags']
    )
    assert membership == ['absent']
    assert compiler.compile_show(exported, {}, [], target) == data


def test_program_table_tags_merge_as_a_union(
    show: dict[str, object], target: models.Target
) -> None:
    table = {
        'patches': [
            {
                'instrument': 'synth',
                'setup': 'default',
                'program': 1,
                'tags': ['whole-system', 'sax'],
            }
        ]
    }
    data = compiler.compile_show(show, {}, [table], target)
    exported = models.Show.model_validate(tomlkit.parse(resolved.export(data)).unwrap())
    assert exported.definitions is not None
    assert exported.definitions.programs[0].tags == ['sax', 'whole-system']


@pytest.mark.parametrize('icon', ['images/patch.png', ''])
def test_icons_preserve_explicit_owners_through_recompilation(
    show: dict[str, object], target: models.Target, icon: str
) -> None:
    show = models.Show.model_validate(show).model_dump(exclude_none=True)
    show['tags']['sax']['style'] = {'icon': 'images/sax.png'}
    show['definitions']['programs'][0]['style'] = {'icon': 'images/program.png'}
    show['patch_lists']['Song 1'][0]['style'] = {'icon': icon}
    data = compiler.compile_show(show, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['tags']['sax']['style'] == {'icon': 'images/sax.png'}
    assert exported['definitions']['programs'][0]['style'] == {
        'icon': 'images/program.png'
    }
    assert exported['patch_lists']['Song 1'][0]['style'] == {'icon': icon}
    assert 'style' not in exported['patch_lists']['Song 1'][1]
    assert compiler.compile_show(exported, {}, [], target) == data


def test_style_icons_inherit_and_can_be_suppressed() -> None:
    inherited = display.merge_style(
        models.Style(icon='images/sax.png'), models.Style(foreground_color='red')
    )
    assert inherited.icon == 'images/sax.png'
    assert (
        display.merge_style(inherited, models.Style(icon='images/patch.png')).icon
        == 'images/patch.png'
    )
    assert display.merge_style(inherited, models.Style(icon='')).icon == ''
