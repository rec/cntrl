"""New-user setup authoring, defaults and non-destructive file creation."""

import shlex
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_chooser import (
    cli,
    compiler_errors,
    data_files,
    global_config,
    setup_create,
)
from cntrl.patch_chooser.setup_compiler import compile_inputs


def answer(monkeypatch: pytest.MonkeyPatch, values: list[str]) -> list[str]:
    replies = iter(values)
    prompts: list[str] = []

    def respond(prompt: str) -> str:
        prompts.append(prompt)
        return next(replies)

    monkeypatch.setattr('builtins.input', respond)
    return prompts


def test_bare_cli_creates_compilable_commented_project(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr('sys.argv', ['cntrl'])
    prompts = answer(monkeypatch, ['', '', '', '', '', '', ''])
    cli.main()
    assert prompts[0] == 'Target directory [.]: '
    assert (tmp_path / 'setup.toml').exists()
    instrument = tmp_path / 'instruments/synth.toml'
    assert tomlkit.parse(instrument.read_text()).unwrap() == {
        'instruments': {'synth': {'output': 'din', 'channel': 1}}
    }
    assert instrument.read_text().count('#') > 20
    assert (tmp_path / 'doc/instrument-definitions.md').is_file()
    source = data_files.Sources(
        document=['setup.toml'],
        target=[str(ROOT / 'firmware/test/target.json')],
    )
    bundle = compile_inputs(source)
    assert bundle.names == ['setup']
    assert 'cntrl compile setup=setup.toml --target' in capsys.readouterr().out


def test_multiple_instruments_and_reviewed_cancellation_write_nothing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    output = tmp_path / 'new'
    answer(
        monkeypatch, ['Live', 'Bass synth', 'din', '17', '2', 'Keys', '', '', '', 'no']
    )
    setup_create.run(setup_create.Create(output=output))
    assert not output.exists()
    answer(
        monkeypatch,
        [
            'Live',
            '../bad',
            'x' * 32,
            'Bass synth',
            'din',
            '2',
            'BASS SYNTH',
            'Keys',
            '',
            '',
            '',
            'yes',
        ],
    )
    setup_create.run(setup_create.Create(output=output))
    assert tomlkit.parse(
        (output / 'instruments/Bass synth.toml').read_text()
    ).unwrap() == {'instruments': {'Bass synth': {'output': 'din', 'channel': 2}}}
    assert tomlkit.parse((output / 'instruments/Keys.toml').read_text()).unwrap() == {
        'instruments': {'Keys': {'output': 'din', 'channel': 1}}
    }
    invocation = next(
        n.removeprefix('# ')
        for n in (output / 'Live.toml').read_text().splitlines()
        if n.startswith('# cntrl compile ')
    )
    arguments = shlex.split(invocation)
    arguments[arguments.index('TARGET.toml')] = str(ROOT / 'firmware/test/target.json')
    monkeypatch.chdir(output)
    monkeypatch.setattr('sys.argv', arguments)
    cli.main()
    assert (output / 'Live.bin').is_file()


def test_inherited_rig_is_not_materialized_or_modified(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    defaults = global_config.user_path()
    defaults.parent.mkdir()
    text = """brightness = 160
[instruments.synth]
output = 'din'
channel = 4
[patches.sound]
instrument = 'synth'
program = 10
[patch_cycles]
Live = ['sound']
"""
    defaults.write_text(text)
    output = tmp_path / 'new'
    answer(monkeypatch, ['Live', '', ''])
    setup_create.run(setup_create.Create(output=output))
    assert defaults.read_text() == text
    assert tomlkit.parse((output / 'Live.toml').read_text()).unwrap() == {
        'instrument_files': ['instruments/1-synth.toml']
    }
    fragments = sorted((output / 'instruments').glob('*.toml'))
    assert len(fragments) == 1
    assert tomlkit.parse(fragments[0].read_text()).unwrap() == {
        'instruments': {'synth': {}}
    }
    trace = data_files.Trace()
    compile_inputs(
        data_files.Sources(
            document=[str(output / 'Live.toml')],
            target=[str(ROOT / 'firmware/test/target.json')],
        ),
        trace=trace,
    )
    assert trace.defaults['Live'] == defaults.resolve()
    assert defaults.resolve() in trace.files


def test_existing_file_prevents_all_writes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    existing = tmp_path / 'instruments/synth.toml'
    existing.parent.mkdir()
    existing.write_text('keep me')
    answer(monkeypatch, ['', '', '', '', '', ''])
    with pytest.raises(compiler_errors.CompilationError, match='exists'):
        setup_create.run(setup_create.Create(output=tmp_path))
    assert existing.read_text() == 'keep me'
    assert not (tmp_path / 'setup.toml').exists()
    assert not (tmp_path / 'doc').exists()


def test_explicit_setup_create_and_help(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr('sys.argv', ['cntrl', 'setup', 'create', '-o', str(tmp_path)])
    answer(monkeypatch, ['', '', '', '', '', ''])
    cli.main()
    assert (tmp_path / 'setup.toml').exists()
    monkeypatch.setattr('sys.argv', ['cntrl', '--help'])
    capsys.readouterr()
    with pytest.raises(SystemExit) as error:
        cli.main()
    assert error.value.code == 0
    assert 'Create a cntrl setup' not in capsys.readouterr().out


def test_interrupted_input_writes_nothing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def interrupted(prompt: str) -> str:
        raise EOFError

    monkeypatch.setattr('builtins.input', interrupted)
    with pytest.raises(EOFError):
        setup_create.run(setup_create.Create(output=tmp_path / 'new'))
    monkeypatch.setattr(
        'sys.argv', ['cntrl', 'setup', 'create', '-o', str(tmp_path / 'new')]
    )
    with pytest.raises(SystemExit, match='Input ended; setup creation cancelled'):
        cli.main()
    assert not (tmp_path / 'new').exists()


def test_unexpected_end_of_file_reaches_the_crash_reporter(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    failure = EOFError('unexpected end of setup data')

    def fail(command: setup_create.Create) -> None:
        raise failure

    monkeypatch.setattr(setup_create, 'run', fail)
    monkeypatch.setattr('sys.argv', ['cntrl', 'setup', 'create'])
    with pytest.raises(EOFError) as caught:
        cli.main()
    assert caught.value is failure


ROOT = Path(__file__).resolve().parents[1]
