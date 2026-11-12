"""Qualification delegates to an explicit target and preserves its arguments."""

import os
import sys
from pathlib import Path

import pytest

from cntrl.patch_switcher import cli


@pytest.mark.parametrize(
    'arguments', [[], ['--help'], ['--run', '--destination', 'port with spaces']]
)
def test_selected_qualification_preserves_arguments_and_exit_status(
    arguments: list[str],
    monkeypatch: pytest.MonkeyPatch,
    capfd: pytest.CaptureFixture[str],
) -> None:
    target = Path(__file__).parent / 'fixtures/build_target'
    monkeypatch.setattr(
        sys, 'argv', ['cntrl', 'qualify', '--target-dir', str(target), '--', *arguments]
    )
    with pytest.raises(SystemExit) as error:
        cli.main()
    assert error.value.code == 7
    assert capfd.readouterr().out == ''.join(a + os.linesep for a in arguments)


def test_qualification_rejects_a_target_without_an_entry(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        sys, 'argv', ['cntrl', 'qualify', '--target-dir', str(tmp_path)]
    )
    with pytest.raises(SystemExit, match='does not provide a qualification entry'):
        cli.main()
