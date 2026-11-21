"""Run qualification supplied by the explicitly selected target."""

import os
import sys
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, Field

from . import target_process


class Qualify(BaseModel, frozen=True):
    """Run target qualification, forwarding arguments supplied after --."""

    target_dir: Path
    arguments: Annotated[list[str], tyro.conf.Positional] = Field(default_factory=list)


def run(command: Qualify) -> None:
    entry = target_process.entry_point(
        command.target_dir, 'qualify_entry.py', 'a qualification entry point'
    )
    result = target_process.run(
        [sys.executable, str(entry), *command.arguments],
        check=False,
        env=dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parent.parent)),
    )
    sys.exit(result.returncode)
