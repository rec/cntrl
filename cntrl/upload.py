"""Expose the selected target's upload helper through the common CLI."""

import os
import subprocess
import sys
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, ConfigDict, Field


class Serve(BaseModel, frozen=True):
    """Serve upload requests. Launch manually from Terminal."""


class Upload(BaseModel, frozen=True):
    """Upload an existing firmware image with its expected SHA-256."""

    image: Path
    sha256: str = Field(pattern=r'^[0-9a-f]{64}$')

    model_config = ConfigDict(extra='forbid')


class Bootloader(BaseModel, frozen=True):
    """Enter the selected target's bootloader without uploading."""


class Provision(BaseModel, frozen=True):
    """Install the selected target's pinned uploader without accessing a device."""


class Helper(BaseModel, frozen=True):
    target_dir: Path
    command: (
        Annotated[Serve, tyro.conf.subcommand(name='serve')]
        | Annotated[Upload, tyro.conf.subcommand(name='upload')]
        | Annotated[Bootloader, tyro.conf.subcommand(name='bootloader')]
        | Annotated[Provision, tyro.conf.subcommand(name='provision')]
    )


def run(command: Helper) -> None:
    entry = command.target_dir.resolve() / 'upload_helper_entry.py'
    if not entry.is_file():
        raise ValueError(
            f'{entry}: target does not provide an upload helper entry point'
        )
    options = command.command
    arguments = (
        ['serve']
        if isinstance(options, Serve)
        else ['bootloader']
        if isinstance(options, Bootloader)
        else ['provision']
        if isinstance(options, Provision)
        else ['upload', '--image', str(options.image), '--sha256', options.sha256]
    )
    result = subprocess.run(
        [sys.executable, str(entry), *arguments],
        check=False,
        env=dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parent.parent)),
    )
    sys.exit(result.returncode)
