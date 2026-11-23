"""Last-chance reporting for command and emulator exceptions."""

import platform
import struct
import subprocess
import sys
import sysconfig
import threading
import traceback
import webbrowser
from datetime import UTC, datetime
from importlib.metadata import distribution, version
from pathlib import Path
from queue import Empty, Queue
from types import TracebackType
from typing import Literal
from urllib.parse import urlencode

native_emulator_ref: str = ''
ISSUE_FORM = 'https://github.com/rec/cntrl/issues/new'
MAX_PREFILLED_URL_LENGTH = 4096
REPORT_OPERATION_TIMEOUT = 5.0


def install(binary: Literal['cntrl', 'emulator']) -> None:
    global native_emulator_ref
    native_emulator_ref = ''

    def unhandled(
        kind: type[BaseException], error: BaseException, trace: TracebackType | None
    ) -> None:
        sys.__excepthook__(kind, error, trace)
        if isinstance(error, (KeyboardInterrupt, SystemExit)):
            return
        report(binary, error, trace)

    def thread_unhandled(args: threading.ExceptHookArgs) -> None:
        if args.exc_type is SystemExit or args.exc_value is None:
            return
        unhandled(args.exc_type, args.exc_value, args.exc_traceback)

    sys.excepthook = unhandled
    threading.excepthook = thread_unhandled


def report(binary: str, error: BaseException, trace: TracebackType | None) -> None:
    """Review a crash report locally before opening its GitHub form."""
    try:
        title = f'Stack trace in {binary}'
        body = issue_body(binary, error, trace)
        url = f'{ISSUE_FORM}?{urlencode({"title": title, "body": body})}'
        prefilled = len(url) <= MAX_PREFILLED_URL_LENGTH
        if not review_report(binary, title, body, prefilled):
            return
        if not prefilled:
            url = f'{ISSUE_FORM}?{urlencode({"title": title})}'
        if not _open_issue_form(url):
            print('Could not open the crash report in a browser.', file=sys.stderr)
    except (OSError, subprocess.CalledProcessError, webbrowser.Error) as failure:
        print(f'Could not open the crash report: {failure}', file=sys.stderr)


def review_report(binary: str, title: str, body: str, prefilled: bool) -> bool:
    copy_notice = (
        'Copy the full report before opening the form, then paste it into GitHub.'
    )
    if binary == 'emulator' and threading.current_thread() is threading.main_thread():
        try:
            from PySide6 import QtWidgets
        except ImportError:
            pass
        else:
            if isinstance(QtWidgets.QApplication.instance(), QtWidgets.QApplication):
                dialog = QtWidgets.QDialog()
                dialog.setWindowTitle(title)
                dialog.resize(720, 480)
                layout = QtWidgets.QVBoxLayout(dialog)
                notice = QtWidgets.QLabel(
                    'Review the report below. Opening the form shares these details '
                    'with GitHub. You can edit the issue before clicking Create.'
                    if prefilled
                    else copy_notice
                )
                notice.setWordWrap(True)
                layout.addWidget(notice)
                preview = QtWidgets.QPlainTextEdit(body)
                preview.setReadOnly(True)
                layout.addWidget(preview)
                buttons = QtWidgets.QDialogButtonBox()
                report_button = buttons.addButton(
                    'Report Issue', QtWidgets.QDialogButtonBox.ButtonRole.AcceptRole
                )
                close_button = buttons.addButton(
                    'Close', QtWidgets.QDialogButtonBox.ButtonRole.RejectRole
                )
                report_button.setAutoDefault(False)
                close_button.setDefault(True)
                buttons.accepted.connect(dialog.accept)
                buttons.rejected.connect(dialog.reject)
                layout.addWidget(buttons)
                close_button.setFocus()
                return dialog.exec() == QtWidgets.QDialog.DialogCode.Accepted

    print(f'{title}\n\n{body}', file=sys.stderr)
    if (
        threading.current_thread() is not threading.main_thread()
        or sys.stdin is None
        or sys.stderr is None
        or not sys.stdin.isatty()
        or not sys.stderr.isatty()
    ):
        return False
    print(
        'Share these details with GitHub and open the issue form? [y/N] '
        if prefilled
        else f'{copy_notice}\nOpen the issue form? [y/N] ',
        end='',
        file=sys.stderr,
        flush=True,
    )
    try:
        return input().strip().casefold() in ('y', 'yes')
    except (EOFError, KeyboardInterrupt):
        return False


def issue_body(binary: str, error: BaseException, trace: TracebackType | None) -> str:
    stack = ''.join(traceback.format_exception(type(error), error, trace))
    details = {
        'Program': binary,
        'CNTRL version': version('cntrl'),
        'Build gitref': build_ref(),
        'Time (UTC)': datetime.now(UTC).isoformat(),
        'Python version': sys.version,
        'Python implementation': platform.python_implementation(),
        'Python build': repr(platform.python_build()),
        'Python compiler': platform.python_compiler(),
        'Python executable': sys.executable,
        'Python ABI': str(sysconfig.get_config_var('SOABI')),
        'Python platform': sysconfig.get_platform(),
        'OS': platform.platform(),
        'Machine': platform.machine(),
        'Pointer width': f'{struct.calcsize("P") * 8} bits',
        'Byte order': sys.byteorder,
        'Filesystem encoding': sys.getfilesystemencoding(),
        'Thread': threading.current_thread().name,
    }
    if binary == 'emulator':
        details['Native emulator gitref'] = native_emulator_ref or 'unknown'
    metadata = '\n'.join(
        f'- **{k}:** {v.replace(chr(10), " ")}' for k, v in details.items()
    )
    # Indentation keeps arbitrary exception text inside the final Markdown appendix.
    appendix = '\n'.join(f'    {s}' for s in stack.splitlines())
    return (
        f'An unhandled Python exception occurred in {binary}.\n\n'
        f'## Runtime details\n\n{metadata}\n\n'
        f'## Appendix: full stack trace\n\n{appendix}\n'
    )


def build_ref() -> str:
    try:
        root = Path(__file__).resolve().parent.parent
        if (root / '.git').exists():
            return subprocess.check_output(
                ['git', '-C', str(root), 'rev-parse', 'HEAD'],
                text=True,
                timeout=REPORT_OPERATION_TIMEOUT,
            ).strip()
        return (
            distribution('cntrl').locate_file('cntrl/_git_ref.txt').read_text().strip()
            or 'unknown'
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return 'unknown'


def _open_issue_form(url: str) -> bool:
    result: Queue[bool | Exception] = Queue(maxsize=1)

    def launch() -> None:
        try:
            result.put_nowait(webbrowser.open(url))
        except Exception as error:
            # Deliver failures to the reporting thread without invoking its hook again.
            result.put_nowait(error)

    threading.Thread(target=launch, daemon=True).start()
    try:
        opened = result.get(timeout=REPORT_OPERATION_TIMEOUT)
    except Empty:
        raise TimeoutError('browser launch timed out') from None
    if isinstance(opened, Exception):
        raise opened
    return opened
