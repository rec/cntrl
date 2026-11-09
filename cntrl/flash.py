"""Build and flash an explicitly selected target through its local entry point."""

import os
import subprocess
import sys
from pathlib import Path

from . import build


class Flash(build.Build, frozen=True):
    port: Path
    legacy_usb: bool = False


def run(command: Flash) -> None:
    entry = command.target_dir.resolve() / 'flash_entry.py'
    if not entry.is_file():
        raise ValueError(f'{entry}: target does not provide a flash entry point')
    build.run(command)
    output = command.output.resolve()
    arguments = [
        sys.executable,
        str(entry),
        '--directory',
        str(output),
        '--port',
        str(command.port),
    ]
    if command.legacy_usb:
        arguments.append('--legacy-usb')
    subprocess.run(
        arguments,
        check=True,
        env=dict(
            os.environ,
            PYTHONPATH=str(command.source_root.resolve()),
            TMPDIR=str(output / 'tmp'),
            TMP=str(output / 'tmp'),
            TEMP=str(output / 'tmp'),
        ),
    )
