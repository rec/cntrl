"""Validation errors keep actionable details at the cntrl command line."""

from pathlib import Path

import pytest

from cntrl.patch_chooser import cli


@pytest.mark.parametrize('flags', [[], ['--verbose'], ['--verbose', '--no-verbose']])
def test_invalid_command_options_show_full_diagnostics_only_when_verbose(
    flags: list[str],
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    monkeypatch.setattr(
        'sys.argv',
        ['/path/to/cntrl', *flags, 'emulate', 'setup.toml', '--port', 'unused'],
    )
    with pytest.raises(SystemExit) as caught:
        cli.main()
    assert caught.value.code == 2
    error = capsys.readouterr().err
    assert 'supply a setup file or --port, not both' in error
    if flags == ['--verbose']:
        assert '1 validation error for Emulate' in error
        assert '[type=value_error,' in error
        assert 'input_type=dict' in error
    else:
        assert 'For full helptext, see cntrl --help' in error
        assert 'Value error,' not in error
        assert 'input_type' not in error
        assert 'pydantic.dev' not in error


@pytest.mark.parametrize('verbose', [False, True])
def test_invalid_restore_names_the_file_and_field(
    verbose: bool, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    path = tmp_path / 'globals.toml'
    path.write_text('language = 123\n')
    flags = ['--verbose'] if verbose else []
    monkeypatch.setattr('sys.argv', ['cntrl', *flags, 'global', 'restore', str(path)])
    with pytest.raises(SystemExit) as caught:
        cli.main()
    error = str(caught.value)
    assert str(path) in error
    assert 'Input should be a valid string' in error
    assert ('input_value=123' in error) == verbose
    if not verbose:
        assert 'language: Input should be a valid string' in error


@pytest.mark.parametrize('verbose', [False, True])
def test_invalid_target_keeps_all_compiler_diagnostics(
    verbose: bool, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    (tmp_path / 'target.toml').write_text('buttons = ["A", "A"]\n')
    (tmp_path / 'setup.toml').write_text('')
    monkeypatch.chdir(tmp_path)
    flags = ['--verbose'] if verbose else []
    monkeypatch.setattr(
        'sys.argv',
        [
            'cntrl',
            *flags,
            'compile',
            'setup.toml',
            '--target',
            'target.toml',
            '--output',
            'setup.bin',
        ],
    )
    with pytest.raises(SystemExit) as caught:
        cli.main()
    error = str(caught.value)
    assert 'Configuration errors' in error
    assert 'input identifiers must be unique printable ASCII without +' in error
    assert ('input_value' in error) == verbose
    if not verbose:
        assert 'target.memory: Field required' in error
        assert 'target.buttons: input identifiers must be unique' in error
