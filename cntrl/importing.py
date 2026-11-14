"""Run the importer supplied by an explicitly selected target."""

import os
import subprocess
import sys
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, Field


class Import(BaseModel, frozen=True):
    """Import target data, forwarding arguments supplied after --."""

    target_dir: Path
    arguments: Annotated[list[str], tyro.conf.Positional] = Field(default_factory=list)


def run(command: Import) -> None:
    entry = command.target_dir.resolve() / 'import_entry.py'
    if not entry.is_file():
        raise ValueError(f'{entry}: target does not provide an import entry point')
    result = subprocess.run(
        [sys.executable, str(entry), *command.arguments],
        check=False,
        env=dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parent.parent)),
    )
    sys.exit(result.returncode)
