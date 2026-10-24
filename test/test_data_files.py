"""Shared expression syntax, origin defaults and aggregate merge errors."""

from pathlib import Path

import pytest

from cntrl.patch_switcher import compiler, data_files


@pytest.mark.parametrize('prefix', ['Evening=', 'A==B=', r'A\=B='])
def test_name_applies_to_complete_expression(prefix: str) -> None:
    result = data_files.expressions(
        [prefix + 'jazz.toml', '+', 'classical.json', 'theatre.toml']
    )
    assert [e.name for e in result] == [
        ('Evening' if prefix == 'Evening=' else 'A=B'),
        'theatre',
    ]
    assert result[0].paths == [Path('jazz.toml'), Path('classical.json')]


def test_default_names_and_literal_plus_and_spaces() -> None:
    result = data_files.expressions(
        ['jazz.toml', '+', 'classical.json', '+', 'night show.toml', 'one+two.toml']
    )
    assert [e.name for e in result] == ['jazz+classical+night show', 'one+two']


@pytest.mark.parametrize(
    'arguments',
    [
        ['+'],
        ['a.toml', '+'],
        ['a.toml', '+', '+', 'b.toml'],
        ['a.toml', '+', 'Name=b.toml'],
    ],
)
def test_invalid_expressions_are_rejected(arguments: list[str]) -> None:
    with pytest.raises(compiler.CompilationError):
        data_files.expressions(arguments)


@pytest.mark.parametrize(
    'name',
    [
        '',
        '.',
        '..',
        '../bad',
        'a/b',
        'a\\b',
        'a:',
        'a*',
        'a?',
        'a\n',
        'café',
        'a' * 64,
        'a.',
        'a ',
    ],
)
def test_unsafe_setup_names_are_rejected(name: str) -> None:
    with pytest.raises(ValueError):
        data_files.validate_name(name)


def test_merges_lists_scalars_and_nested_dictionaries(tmp_path: Path) -> None:
    first, second = tmp_path / 'one.toml', tmp_path / 'two.toml'
    first.write_text('items = [1]\nvalue = 1\n[nested]\na = 1\n')
    second.write_text('items = [1, 2]\nvalue = 2\n[nested]\nb = 2\n')
    expression = data_files.expressions([str(first), '+', str(second)])[0]
    errors: list[str] = []
    assert data_files.load(expression, True, errors) == {
        'items': [1, 1, 2],
        'value': 2,
        'nested': {'a': 1, 'b': 2},
    }
    assert not errors
    data_files.load(expression, False, errors)
    assert len(errors) == 2
    assert all(str(first) in e and str(second) in e for e in errors)


def test_strict_merge_retains_equal_values_and_finds_all_conflicts(
    tmp_path: Path,
) -> None:
    files = [tmp_path / f'{i}.toml' for i in range(3)]
    for p in files:
        p.write_text('same = [1]\n[nested]\nvalue = 1\nother = false\n')
    files[1].write_text('same = [1]\n[nested]\nvalue = 2\nother = 1\n')
    files[2].write_text('same = [2]\n[nested]\nvalue = 3\n')
    expression = data_files.DataFile(name='test', paths=files)
    errors: list[str] = []
    result = data_files.load(expression, False, errors)
    assert result['same'] == [1]
    assert len(errors) == 4
    assert any('nested.other' in e and 'incompatible' in e for e in errors)


def test_origin_defaults_are_resolved_before_combining_tables(tmp_path: Path) -> None:
    files = [tmp_path / f'{n}.toml' for n in ('pman', 'vl70')]
    for p in files:
        p.write_text('[[patches]]\nprogram = 1\n')
    errors: list[str] = []
    result = data_files.load(
        data_files.DataFile(name='Evening', paths=files), True, errors
    )
    assert result['patches'] == [
        {'program': 1, 'instrument': 'pman'},
        {'program': 1, 'instrument': 'vl70'},
    ]
    assert not errors
