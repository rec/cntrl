"""Crash reports keep complete diagnostics without contacting GitHub in tests."""

import subprocess
import sys
import threading
from pathlib import Path
from unittest.mock import Mock
from urllib.parse import parse_qs, urlsplit

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
    monkeypatch.setattr(crashes, 'native_emulator_ref', 'b' * 7)
    error = chained_error()
    body = crashes.issue_body(binary, error, error.__traceback__)
    assert f'An unhandled Python exception occurred in {binary}.' in body
    assert f'**Build gitref:** {"a" * 40}' in body
    if binary == 'emulator':
        assert f'**Native emulator gitref:** {"b" * 7}' in body
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


@pytest.mark.parametrize('binary', ['cntrl', 'emulator'])
def test_report_opens_prefilled_issue_form(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], binary: str
) -> None:
    monkeypatch.setattr(crashes, 'build_ref', lambda: 'build-ref')
    monkeypatch.setattr(sys.stdin, 'isatty', lambda: True)
    monkeypatch.setattr(sys.stderr, 'isatty', lambda: True)
    urls: list[str] = []

    def approve() -> str:
        assert not urls
        preview = capsys.readouterr().err
        assert f'Stack trace in {binary}' in preview
        assert 'Échec & détails #1?' in preview
        return 'yes'

    def open_browser(url: str) -> bool:
        urls.append(url)
        return True

    monkeypatch.setattr('builtins.input', approve)
    monkeypatch.setattr(crashes.webbrowser, 'open', open_browser)
    error = chained_error()
    error.add_note('Échec & détails #1?')
    crashes.report(binary, error, error.__traceback__)
    (url,) = urls
    form = urlsplit(url)
    assert (form.scheme, form.netloc, form.path) == (
        'https',
        'github.com',
        '/rec/cntrl/issues/new',
    )
    fields = parse_qs(form.query)
    assert fields['title'] == [f'Stack trace in {binary}']
    (body,) = fields['body']
    assert '**Build gitref:** build-ref' in body
    assert 'ValueError: original failure' in body
    assert body.endswith('    Échec & détails #1?\n')


def test_large_report_opens_title_only_and_keeps_full_text_local(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(crashes, 'build_ref', lambda: 'build-ref')
    monkeypatch.setattr(sys.stdin, 'isatty', lambda: True)
    monkeypatch.setattr(sys.stderr, 'isatty', lambda: True)
    monkeypatch.setattr('builtins.input', lambda: 'yes')
    browser = Mock(return_value=True)
    monkeypatch.setattr(crashes.webbrowser, 'open', browser)
    message = 'Échec ' * 1000
    crashes.report('cntrl', RuntimeError(message), None)
    assert browser.call_count == 1
    form = urlsplit(browser.call_args.args[0])
    assert parse_qs(form.query) == {'title': ['Stack trace in cntrl']}
    preview = capsys.readouterr().err
    assert message in preview
    assert 'Copy the full report before opening' in preview


@pytest.mark.parametrize('interactive', [True, False])
def test_terminal_report_stays_local_without_approval(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    interactive: bool,
) -> None:
    monkeypatch.setattr(crashes, 'build_ref', lambda: 'build-ref')
    monkeypatch.setattr(sys.stdin, 'isatty', lambda: interactive)
    monkeypatch.setattr(sys.stderr, 'isatty', lambda: interactive)
    monkeypatch.setattr(
        'builtins.input',
        (lambda: '')
        if interactive
        else (lambda: pytest.fail('prompted without a terminal')),
    )
    monkeypatch.setattr(
        crashes.webbrowser, 'open', lambda url: pytest.fail('opened without approval')
    )
    error = chained_error()
    crashes.report('cntrl', error, error.__traceback__)
    assert 'RuntimeError: outer failure' in capsys.readouterr().err


@pytest.mark.parametrize(
    'failure',
    [None, crashes.webbrowser.Error('browser launch failed')],
)
def test_reporting_failure_preserves_original_traceback(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    failure: Exception | None,
) -> None:
    monkeypatch.setattr(crashes, 'build_ref', lambda: 'build-ref')
    monkeypatch.setattr(crashes, 'review_report', lambda *args: True)

    def open_browser(url: str) -> bool:
        if failure is not None:
            raise failure
        return False

    monkeypatch.setattr(crashes.webbrowser, 'open', open_browser)
    monkeypatch.setattr(sys, 'excepthook', sys.excepthook)
    monkeypatch.setattr(threading, 'excepthook', threading.excepthook)
    crashes.install('cntrl')
    error = chained_error()
    sys.excepthook(type(error), error, error.__traceback__)
    output = capsys.readouterr().err
    assert 'ValueError: original failure' in output
    assert 'RuntimeError: outer failure' in output
    assert 'Could not open the crash report' in output


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


def test_unstamped_archive_reports_unknown_revision(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(crashes, '__file__', str(tmp_path / 'cntrl' / 'crashes.py'))
    monkeypatch.setattr(
        crashes, 'distribution', lambda _: Mock(locate_file=tmp_path.joinpath)
    )
    assert '**Build gitref:** unknown' in crashes.issue_body(
        'cntrl', ValueError(), None
    )
