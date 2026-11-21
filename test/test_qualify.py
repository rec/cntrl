"""Qualification delegates to an explicit target and preserves its arguments."""

import os
import subprocess
import sys
from pathlib import Path

import pytest

from cntrl import qualify
from cntrl.patch_chooser import cli


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


def test_target_entry_launch_failure_reports_the_error(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    target = Path(__file__).parent / 'fixtures/build_target'

    def denied(*args: object, **kwargs: object) -> None:
        raise PermissionError('cannot launch selected target')

    monkeypatch.setattr(subprocess, 'run', denied)
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'qualify', '--target-dir', str(target)])
    with pytest.raises(SystemExit, match='cannot launch selected target'):
        cli.main()


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_target_adapter_command_preserves_unexpected_failures(
    monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected qualification failure')

    def fail(command: qualify.Qualify) -> None:
        raise failure

    monkeypatch.setattr(qualify, 'run', fail)
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'qualify', '--target-dir', '/unused'])
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure
