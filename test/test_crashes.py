"""Crash reports keep complete diagnostics without contacting GitHub in tests."""

import subprocess
import sys
import threading
from pathlib import Path

import pytest

from cntrl import crashes, entrypoint
from cntrl.patch_chooser import cli


def chained_error() -> RuntimeError:
    try:
        try:
            raise ValueError('original failure')
        except ValueError as cause:
            raise RuntimeError('outer failure\n```\nexception text') from cause
    except RuntimeError as error:
        return error


@pytest.mark.parametrize('binary', ['cntrl', 'emulator'])
def test_report_contains_build_platform_and_complete_traceback(
    monkeypatch: pytest.MonkeyPatch, binary: str
) -> None:
    monkeypatch.setattr(crashes, 'build_ref', lambda: 'a' * 40)
    error = chained_error()
    body = crashes.issue_body(binary, error, error.__traceback__)
    assert f'An unhandled Python exception occurred in {binary}.' in body
    assert f'**Build gitref:** {"a" * 40}' in body
    for label in (
        'Python version',
        'Python build',
        'Python compiler',
        'Python ABI',
        'Python platform',
        'OS',
        'Machine',
        'Pointer width',
    ):
        assert f'**{label}:**' in body
    appendix = body.split('## Appendix: full stack trace\n\n')[1]
    assert 'ValueError: original failure' in appendix
    assert 'The above exception was the direct cause' in appendix
    assert 'RuntimeError: outer failure' in appendix
    assert 'test_crashes.py' in appendix
    assert appendix.endswith('    exception text\n')
    assert all(s.startswith('    ') for s in appendix.splitlines())


def test_report_files_issue_using_stdin(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(crashes, 'build_ref', lambda: 'build-ref')
    calls: list[tuple[list[str], dict[str, object]]] = []

    def run(args: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        calls.append((args, kwargs))
        return subprocess.CompletedProcess(
            args, 0, 'https://github.com/rec/cntrl/issues/123\n', ''
        )

    monkeypatch.setattr(crashes.subprocess, 'run', run)
    error = chained_error()
    crashes.report('emulator', error, error.__traceback__)
    args, kwargs = calls[0]
    assert args == [
        'gh',
        'issue',
        'create',
        '--repo',
        'rec/cntrl',
        '--title',
        'Stack trace in emulator',
        '--body-file',
        '-',
    ]
    assert 'RuntimeError: outer failure' in str(kwargs['input'])
    assert kwargs['encoding'] == 'utf-8'
    assert 'https://github.com/rec/cntrl/issues/123' in capsys.readouterr().err


@pytest.mark.parametrize(
    'failure',
    [
        FileNotFoundError('gh missing'),
        subprocess.CalledProcessError(1, 'gh', stderr='authentication required'),
    ],
)
def test_reporting_failure_preserves_original_traceback(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    failure: Exception,
) -> None:
    monkeypatch.setattr(crashes, 'build_ref', lambda: 'build-ref')

    def run(*args: object, **kwargs: object) -> None:
        raise failure

    monkeypatch.setattr(crashes.subprocess, 'run', run)
    monkeypatch.setattr(sys, 'excepthook', sys.excepthook)
    monkeypatch.setattr(threading, 'excepthook', threading.excepthook)
    crashes.install('cntrl')
    error = chained_error()
    sys.excepthook(type(error), error, error.__traceback__)
    output = capsys.readouterr().err
    assert 'ValueError: original failure' in output
    assert 'RuntimeError: outer failure' in output
    assert 'Could not file crash issue:' in output


@pytest.mark.parametrize(
    'arguments,binary',
    [(['cntrl', 'status'], 'cntrl'), (['cntrl', 'emulate', 'setup.toml'], 'emulator')],
)
def test_entrypoint_installs_handler_before_running_command(
    monkeypatch: pytest.MonkeyPatch, arguments: list[str], binary: str
) -> None:
    installed: list[str] = []
    monkeypatch.setattr(sys, 'argv', arguments)
    monkeypatch.setattr(entrypoint, 'install', installed.append)

    def fail() -> None:
        assert installed == [binary]
        raise RuntimeError('command failure')

    monkeypatch.setattr(cli, 'main', fail)
    with pytest.raises(RuntimeError, match='command failure'):
        entrypoint.main()


def test_background_thread_uses_same_reporter(monkeypatch: pytest.MonkeyPatch) -> None:
    reported: list[str] = []
    monkeypatch.setattr(sys, 'excepthook', sys.excepthook)
    monkeypatch.setattr(threading, 'excepthook', threading.excepthook)
    monkeypatch.setattr(
        crashes, 'report', lambda binary, error, trace: reported.append(str(error))
    )
    crashes.install('emulator')
    error = RuntimeError('reader failure')
    threading.excepthook(
        threading.ExceptHookArgs(
            (RuntimeError, error, None, threading.current_thread())
        )
    )
    assert reported == ['reader failure']


def test_keyboard_interrupt_does_not_file_issue(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(sys, 'excepthook', sys.excepthook)
    monkeypatch.setattr(threading, 'excepthook', threading.excepthook)
    monkeypatch.setattr(
        crashes, 'report', lambda *args: pytest.fail('reported interrupt')
    )
    crashes.install('cntrl')
    sys.excepthook(KeyboardInterrupt, KeyboardInterrupt(), None)


def test_checkout_revision_matches_git() -> None:
    root = Path(__file__).resolve().parents[1]
    expected = subprocess.check_output(
        ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True
    ).strip()
    assert crashes.build_ref() == expected
