"""Run the importer supplied by an explicitly selected target."""

import os
import sys
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, Field

from . import target_process


class Import(BaseModel, frozen=True):
    """Import target data, forwarding arguments supplied after --."""

    target_dir: Path
    arguments: Annotated[list[str], tyro.conf.Positional] = Field(default_factory=list)


def run(command: Import) -> None:
    entry = target_process.entry_point(
        command.target_dir, 'import_entry.py', 'an import entry point'
    )
    result = target_process.run(
        [sys.executable, str(entry), *command.arguments],
        check=False,
        env=dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parent.parent)),
    )
    sys.exit(result.returncode)
