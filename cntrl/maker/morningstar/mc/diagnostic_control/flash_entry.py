"""Use the control export's matching compiler for backup, flash and restoration."""

import os
import subprocess
import sys
from pathlib import Path

import tyro
from pydantic import BaseModel


class Flash(BaseModel, frozen=True):
    directory: Path
    port: Path
    legacy_usb: bool = False


def run(command: Flash) -> None:
    directory = command.directory.resolve()
    source = directory.with_name(directory.name.replace(' ', '_') + '-source')
    subprocess.run(
        [
            sys.executable,
            str(source / 'cntrl/maker/morningstar/mc/flash_entry.py'),
            '--directory',
            str(directory),
            '--port',
            str(command.port),
            *(['--legacy-usb'] if command.legacy_usb else []),
        ],
        check=True,
        env=dict(os.environ, PYTHONPATH=str(source)),
    )


if __name__ == '__main__':
    run(tyro.cli(Flash))
