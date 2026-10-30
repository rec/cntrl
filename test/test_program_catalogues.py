"""Compact catalogues retain MIDI addresses and exact program-name lookup."""

from copy import deepcopy
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, data_files, models, resolved


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )


@pytest.fixture
def show() -> dict[str, object]:
    return {
        'instruments': {
            'synth': {
                'output': 'din',
                'channel': 1,
                'programs': [
                    'Flute',
                    {},
                    {'name': 'Lead', 'tags': 'synth: solo, bright'},
                ],
            }
        },
        'patch_lists': {'Live': ['Flute', 'Lead']},
    }


def test_flat_catalogue_preserves_holes_and_omits_bank_selection(
    show: dict[str, object], target: models.Target
) -> None:
    before = deepcopy(show)
    data = compiler.compile_show(show, {}, [], target)
    assert show == before
    first, third = resolved.patch(data, 0), resolved.patch(data, 1)
    assert (first['program'], third['program']) == (1, 3)
    assert 'bank' not in first and 'bank' not in third
    assert data[76 + 68 + 68 + 5 : 76 + 68 + 68 + 7] == b'\xff\xff'
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['definitions']['programs'][1]['tags'] == ['synth', 'solo', 'bright']
    assert compiler.compile_show(exported, {}, [], target) == data


def test_keyed_bank_uses_configured_controller_mapping(target: models.Target) -> None:
    show = {
        'instruments': {
            'synth': {'channel': 1, 'output': 'din', 'programs': {'3': ['Flute']}}
        },
        'patch_lists': {'Live': ['Flute']},
    }
    definitions = {
        'setups': [
            {'instrument': 'synth', 'name': 'default', 'banks': {'3': {'lsb': 3}}}
        ]
    }
    data = compiler.compile_show(show, definitions, [], target)
    assert resolved.patch(data, 0)['bank'] == 3
    assert data[76 + 68 + 68 + 5 : 76 + 68 + 68 + 7] == b'\xff\x03'
    with pytest.raises(compiler.CompilationError, match='no mapping'):
        compiler.compile_show(show, {}, [], target)


def test_name_lookup_is_exact_and_qualifies_ambiguous_instruments(
    show: dict[str, object], target: models.Target
) -> None:
    show['instruments']['other'] = {
        'output': 'trs',
        'channel': 2,
        'programs': ['Flute', 'Flute II'],
    }
    with pytest.raises(compiler.CompilationError, match='found 2'):
        compiler.compile_show(show, {}, [], target)
    show['patch_lists'] = {'Live': [{'name': 'Flute', 'instrument': 'other'}]}
    data = compiler.compile_show(show, {}, [], target)
    assert resolved.patch(data, 0)['instrument'] == 'other'
    show['patch_lists'] = {'Live': ['Flut']}
    with pytest.raises(compiler.CompilationError, match='found 0'):
        compiler.compile_show(show, {}, [], target)


@pytest.mark.parametrize(
    'programs', [True, {'-1': ['Bad']}, [False], ['x'] * 129, [{'program': 9}]]
)
def test_invalid_catalogues_are_rejected(
    show: dict[str, object], target: models.Target, programs: object
) -> None:
    show['instruments']['synth']['programs'] = programs
    with pytest.raises(compiler.CompilationError, match='programs'):
        compiler.compile_show(show, {}, [], target)


@pytest.mark.parametrize('avoid_confict', [False, True])
@pytest.mark.parametrize('keyed', [False, True])
def test_combining_catalogues_never_appends_program_slots(
    tmp_path: Path, avoid_confict: bool, keyed: bool
) -> None:
    first, second = tmp_path / 'one.toml', tmp_path / 'two.toml'
    header = (
        '[instruments.synth.programs]\n0' if keyed else '[instruments.synth]\nprograms'
    )
    first.write_text(header + " = ['Flute']\n")
    second.write_text(header + " = ['Lead']\n")
    expression = data_files.DataFile(name='Live', paths=[first, second])
    errors: list[str] = []
    data_files.load(expression, avoid_confict, errors)
    assert len(errors) == 1 and 'conflicting program positions' in errors[0]
    second.write_text(first.read_text())
    errors.clear()
    result = data_files.load(expression, avoid_confict, errors)
    programs = result['instruments']['synth']['programs']
    assert (programs['0'] if keyed else programs) == ['Flute']
    assert not errors


def test_compact_catalogue_can_be_loaded_as_an_external_table(
    target: models.Target,
) -> None:
    show = {
        'instruments': {'patchman': {'channel': 1, 'output': 'din'}},
        'patch_lists': {'Live': ['GuitHero']},
    }
    definitions = {
        'setups': [
            {'instrument': 'patchman', 'name': 'default', 'banks': {'3': {'lsb': 3}}}
        ]
    }
    table = compiler.read_document(Path('instruments/patchman-internal.toml'))
    data = compiler.compile_show(show, definitions, [table], target)
    patch = resolved.patch(data, 0)
    assert (patch['bank'], patch['program']) == (3, 1)


def test_preset_and_internal_catalogues_merge_by_bank() -> None:
    errors: list[str] = []
    result = data_files.load(
        data_files.DataFile(
            name='patchman',
            paths=[
                Path('instruments/patchman.toml'),
                Path('instruments/patchman-internal.toml'),
            ],
        ),
        True,
        errors,
    )
    programs = result['instruments']['patchman']['programs']
    assert {k: len(v) for k, v in programs.items()} == {'0': 128, '1': 128, '3': 64}
    assert not errors


def test_flat_example_compiles(target: models.Target) -> None:
    show = compiler.read_document(Path('examples/patch-switcher/program-changes.toml'))
    compiler.compile_show(show, {}, [], target)


@pytest.mark.parametrize('instrument', ['vl70m', 'patchman'])
def test_personal_tags_join_without_modifying_documented_catalogues(
    instrument: str, target: models.Target
) -> None:
    catalogue = compiler.read_document(Path('instruments') / f'{instrument}.toml')
    programs = catalogue['instruments'][instrument]['programs']
    assert all(
        not isinstance(p, dict) or 'tags' not in p for v in programs.values() for p in v
    )
    tags = compiler.read_document(Path('instruments/tags.toml'))
    expected = {
        (p['bank'], p['program']): p['tags']
        for p in tags['patches']
        if p['instrument'] == instrument
    }
    show = {
        'instruments': {instrument: {'channel': 1, 'output': 'din'}},
        'patch_lists': {
            'All': [
                {
                    'type': 'program-change',
                    'instrument': instrument,
                    'bank': int(b),
                    'program': n,
                }
                for b, v in programs.items()
                for n, _ in enumerate(v, 1)
            ]
        },
    }
    definitions = compiler.read_document(Path('instruments/definitions.toml'))
    data = compiler.compile_show(show, definitions, [catalogue, tags], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    for p in exported['definitions']['programs']:
        assert p['tags'] == expected.get((p['bank'], p['program']), [])
