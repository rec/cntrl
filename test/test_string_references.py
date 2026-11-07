"""String references follow the shared Patch parser at every authoring entry."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved, songs


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )


def test_strings_in_every_patch_position_compile_and_round_trip(
    target: models.Target,
) -> None:
    source = compiler.read_document(
        Path('examples/patch-switcher/string-references.toml')
    )
    assert isinstance(source, dict)
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data
    assert exported['patches']['clean-alias']['patch'] == 'clean'
    assert exported['patches']['intro']['patches'][1]['patch'] == 'clean-alias'
    assert [p['patch'] for p in exported['patch_cycles']['Live']] == [
        'clean',
        'lead',
        'intro',
    ]
    switches = exported['switches']
    assert switches['lead']['on']['patch'] == 'lead'
    assert switches['lead']['off']['patch'] == 'clean'
    assert switches['lead']['enter']['action']['patch'] == 'prepare-lead'
    assert switches['lead']['exit']['patch'] == 'finish-lead'
    assert switches['gestures']['gestures']['double_tap']['patch'] == 'intro'
    library = songs.Library.model_validate(
        {
            'patches': source['patches'],
            'songs': [
                {
                    'name': 'Repeated variation',
                    'patches': ['clean', 'lead', 'clean'],
                }
            ],
        }
    )
    encoded = songs.encode(library, target)
    decoded = songs.decode(encoded)
    assert [p['patch'] for p in decoded.songs[0].patches] == ['clean', 'lead', 'clean']
    assert songs.encode(decoded, target) == encoded


def test_browser_strings_select_patch_keys_and_catalogue_names_are_explicit(
    target: models.Target,
) -> None:
    show = {
        'instruments': {'synth': {'output': 'din', 'channel': 1, 'programs': ['lead']}},
        'patches': {'lead': {'controller': 7, 'value': 127}},
        'patch_cycles': {'Live': ['lead', {'name': 'lead'}]},
    }
    exported = tomlkit.parse(
        resolved.export(compiler.compile_show(show, {}, [], target))
    ).unwrap()
    reference, program = exported['patch_cycles']['Live']
    assert reference['patch'] == 'lead'
    assert program['program'] == 1 and program['instrument'] == 'synth'
    show['patches'] = {}
    with pytest.raises(ValueError, match='does not exist'):
        compiler.compile_show(show, {}, [], target)


def test_string_aliases_in_companion_setups_match_table_references(
    target: models.Target,
) -> None:
    show = {
        'patch_cycles': {'Live': [{'document': 'other', 'patch': 'alias'}]},
    }
    document = {
        'patches': {
            'alias': 'lead',
            'lead': {'output': 'din', 'channel': 1, 'controller': 7, 'value': 127},
        }
    }
    data = compiler.compile_show(show, {}, [], target, documents={'other': document})
    document['patches']['alias'] = {'patch': 'lead'}
    assert (
        compiler.compile_show(show, {}, [], target, documents={'other': document})
        == data
    )


def test_string_reference_cycles_are_rejected(target: models.Target) -> None:
    show = {
        'patches': {'a': 'b', 'b': 'a'},
        'switches': {'test': {'button': 'A', 'on': 'a'}},
    }
    with pytest.raises(ValueError, match='cycle'):
        compiler.compile_show(show, {}, [], target)


@pytest.mark.parametrize('value', [1, False, None, ['lead']])
def test_non_strings_are_not_coerced_to_patch_names(value: object) -> None:
    with pytest.raises(ValueError):
        models.SwitchAssignment.model_validate({'button': 'A', 'on': value})
