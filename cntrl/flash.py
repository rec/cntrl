"""Prepare packaged firmware and flash through its physical target adapter."""

import os
import subprocess
import sys
from pathlib import Path
from uuid import uuid4

from pydantic import BaseModel, Field

from .firmware.distribution import controller_directory


class Flash(BaseModel, frozen=True):
    controller: str
    port: Path | None = None
    serial: str | None = Field(default=None, min_length=1)
    output: Path | None = None
    """New persistent installation directory; defaults to .cntrl-install/<id>."""
    resources: Path | None = None
    """Use an explicit maintainer component export instead of the packaged set."""
    first_install: bool = False
    """Confirm the selected physical model and install from supported stock."""
    stock_version: str | None = None
    editor_backup: Path | None = None
    legacy_usb: bool = False
    prepare_only: bool = False
    """Compose and validate the image without opening any device."""


def run(command: Flash) -> None:
    resources = controller_directory(command.controller)
    entry = resources.parent / 'flash_entry.py'
    if not entry.is_file():
        raise ValueError(f'{entry}: target does not provide a flash entry point')
    output = (command.output or Path('.cntrl-install') / uuid4().hex).resolve()
    arguments = [
        sys.executable,
        str(entry),
        '--directory',
        str(output),
    ]
    for name, value in (
        ('port', command.port),
        ('serial', command.serial),
        ('stock-version', command.stock_version),
        ('editor-backup', command.editor_backup),
        ('resources', command.resources),
    ):
        if value is not None:
            arguments.extend([f'--{name}', str(value)])
    for name, enabled in (
        ('first-install', command.first_install),
        ('legacy-usb', command.legacy_usb),
        ('prepare-only', command.prepare_only),
    ):
        if enabled:
            arguments.append(f'--{name}')
    subprocess.run(
        arguments,
        check=True,
        env=dict(
            os.environ,
            PYTHONPATH=str(Path(__file__).resolve().parent.parent),
        ),
    )
