"""The short emulator invocation selects one source of controller geometry."""

from pathlib import Path

import pytest
import tyro

from cntrl.emulator.cli import Emulate, prepare
from cntrl.patch_switcher.cli import compile_to_file


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
