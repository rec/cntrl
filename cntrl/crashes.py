"""Last-chance reporting for command and emulator exceptions."""

import platform
import struct
import subprocess
import sys
import sysconfig
import threading
import traceback
from datetime import UTC, datetime
from importlib.metadata import distribution, version
from pathlib import Path
from types import TracebackType
from typing import Literal


def install(binary: Literal['cntrl', 'emulator']) -> None:
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
    """Publish the complete traceback; reporting failures retain the stderr copy."""
    try:
        result = subprocess.run(
            [
                'gh',
                'issue',
                'create',
                '--repo',
                'rec/cntrl',
                '--title',
                f'Stack trace in {binary}',
                '--body-file',
                '-',
            ],
            input=issue_body(binary, error, trace),
            encoding='utf-8',
            capture_output=True,
            check=True,
        )
    except (OSError, subprocess.CalledProcessError) as failure:
        detail = (
            failure.stderr
            if isinstance(failure, subprocess.CalledProcessError)
            else str(failure)
        )
        print(f'Could not file crash issue: {detail}', file=sys.stderr)
    else:
        print(f'Crash issue: {result.stdout.strip()}', file=sys.stderr)


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
    root = Path(__file__).resolve().parent.parent
    if (root / '.git').exists():
        return subprocess.check_output(
            ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True
        ).strip()
    return distribution('cntrl').locate_file('cntrl/_git_ref.txt').read_text().strip()
