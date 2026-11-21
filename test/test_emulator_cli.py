"""The short emulator invocation selects one source of controller geometry."""

from collections.abc import Callable
from pathlib import Path
from unittest.mock import Mock

import pytest
import tyro

from cntrl.emulator import mirror
from cntrl.emulator.cli import Emulate, prepare
from cntrl.emulator.errors import EmulatorError
from cntrl.emulator.layout import EmulatorLayout
from cntrl.emulator.replay import Scenario
from cntrl.patch_chooser import cli, transport
from cntrl.patch_chooser.cli import compile_to_file


def test_short_invocation_parses_the_setup_as_a_positional_argument() -> None:
    command = tyro.cli(Emulate, args=['performance.toml', '--controller', 'example'])
    assert command.setup == Path('performance.toml')
    assert command.controller == 'example'
    assert command.storage is None


@pytest.mark.parametrize(
    'selection',
    [
        {},
        {'layout': Path('layout.toml')},
        {'controller': 'example', 'target': Path('target.toml')},
    ],
)
def test_incomplete_or_conflicting_profile_choices_are_rejected(
    selection: dict[str, object],
) -> None:
    with pytest.raises(ValueError, match='--controller'):
        Emulate.model_validate({'setup': 'setup.toml', **selection})


def test_unknown_profile_does_not_create_a_session(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.chdir(tmp_path)
    with pytest.raises(ValueError, match='unknown emulator controller'):
        prepare(
            Emulate(setup=Path('unused.toml'), controller='missing'), compile_to_file
        )
    assert not (tmp_path / '.cntrl-emulator').exists()


@pytest.mark.parametrize('load', [EmulatorLayout.load, Scenario.load])
def test_invalid_emulator_input_names_its_file(
    tmp_path: Path,
    load: Callable[[Path], EmulatorLayout | Scenario],
) -> None:
    path = tmp_path / 'input.toml'
    path.write_text('[invalid')
    with pytest.raises(EmulatorError, match='input.toml'):
        load(path)


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_emulator_cli_preserves_unexpected_failures(
    monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected emulator failure')
    monkeypatch.setattr(cli, 'emulate', Mock(side_effect=failure))
    monkeypatch.setattr(
        'sys.argv', ['cntrl', 'emulate', 'setup.toml', '--controller', 'mc6-pro']
    )
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure


def test_mirror_preserves_unexpected_host_file_failures(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    failure = FileNotFoundError('unexpected host file missing')
    client = Mock(spec=transport.SetupClient)
    client.request.return_value = 'CNTRL MIRROR 1'
    client.download_state.side_effect = failure
    path = '/cntrl/setups/bundle/0.bin'
    monkeypatch.setattr(
        mirror, 'storage_manifest', Mock(return_value={path: (path, 12)})
    )
    with pytest.raises(FileNotFoundError) as caught:
        mirror.capture(client, tmp_path, 'CNTRL READY')
    assert caught.value is failure
