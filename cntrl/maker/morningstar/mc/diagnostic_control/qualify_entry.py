"""Use the control's existing setup CLI to prepare and restore qualification data."""

import os
import subprocess
import sys
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel


class Setup(BaseModel, frozen=True):
    directory: Path
    """Control export with its retained adjacent source directory."""
    arguments: Annotated[list[str], tyro.conf.Positional]
    """Existing setup arguments, following a second -- separator."""


def run(command: Setup) -> None:
    directory = command.directory.resolve()
    source = directory.with_name(directory.name + '-source').resolve(strict=True)
    (source / 'cntrl/patch_switcher/cli.py').resolve(strict=True)
    subprocess.run(
        [
            sys.executable,
            '-P',
            '-m',
            'cntrl.patch_switcher.cli',
            'setup',
            *command.arguments,
        ],
        check=True,
        env=dict(os.environ, PYTHONPATH=str(source)),
    )


if __name__ == '__main__':
    run(tyro.cli(Setup))
