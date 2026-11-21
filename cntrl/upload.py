"""Expose the selected target's upload helper through the common CLI."""

import os
import sys
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, ConfigDict, Field

from . import target_process
from .firmware.distribution import controller_directory


class Serve(BaseModel, frozen=True):
    """Serve upload requests. Launch manually from Terminal."""

    serial: str = Field(min_length=1)


class Upload(BaseModel, frozen=True):
    """Upload an existing firmware image with its expected SHA-256."""

    image: Path
    sha256: str = Field(pattern=r'^[0-9a-f]{64}$')
    serial: str = Field(min_length=1)

    model_config = ConfigDict(extra='forbid')


class Bootloader(BaseModel, frozen=True):
    """Enter the selected target's bootloader without uploading."""

    serial: str = Field(min_length=1)


class Provision(BaseModel, frozen=True):
    """Install the selected target's pinned uploader without accessing a device."""


class Helper(BaseModel, frozen=True):
    controller: str
    command: (
        Annotated[Serve, tyro.conf.subcommand(name='serve')]
        | Annotated[Upload, tyro.conf.subcommand(name='upload')]
        | Annotated[Bootloader, tyro.conf.subcommand(name='bootloader')]
        | Annotated[Provision, tyro.conf.subcommand(name='provision')]
    )


def run(command: Helper) -> None:
    entry = target_process.entry_point(
        controller_directory(command.controller).parent,
        'upload_helper_entry.py',
        'an upload helper entry point',
    )
    options = command.command
    arguments = (
        ['serve', '--serial', options.serial]
        if isinstance(options, Serve)
        else ['bootloader', '--serial', options.serial]
        if isinstance(options, Bootloader)
        else ['provision']
        if isinstance(options, Provision)
        else [
            'upload',
            '--image',
            str(options.image),
            '--sha256',
            options.sha256,
            '--serial',
            options.serial,
        ]
    )
    result = target_process.run(
        [sys.executable, str(entry), *arguments],
        check=False,
        env=dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parent.parent)),
    )
    sys.exit(result.returncode)
