"""Recognize expected failures at the selected target's process boundary."""

import subprocess
from pathlib import Path


class TargetAdapterError(ValueError):
    """The selected target could not be invoked or did not provide its output."""


def entry_point(directory: Path, filename: str, description: str) -> Path:
    try:
        entry = directory.resolve() / filename
        present = entry.is_file()
    except OSError as error:
        raise TargetAdapterError(str(error)) from error
    if not present:
        raise TargetAdapterError(f'{entry}: target does not provide {description}')
    return entry


def run(
    arguments: list[str], *, check: bool, env: dict[str, str]
) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(arguments, check=check, env=env)
    except OSError as error:
        raise TargetAdapterError(str(error)) from error
