"""Build an explicitly selected implementation through its executable contract."""

import os
import sys
from pathlib import Path

import tomlkit
from pydantic import BaseModel, Field, ValidationError
from tomlkit.exceptions import ParseError

from . import target_process


class Build(BaseModel, frozen=True):
    target_dir: Path
    output: Path
    source_root: Path = Field(default_factory=Path.cwd)
    diagnostics: bool = False
    publish: bool = False
    """Publish verified own-code resources into the selected adapter's package."""


class Artifacts(BaseModel, frozen=True):
    """Paths relative to the build output, retained when it is relocated."""

    image: str
    target: str


def read_artifacts(output: Path) -> Artifacts:
    """Validate an existing export and its files inside the output directory."""
    try:
        output = output.resolve()
        artifacts = Artifacts.model_validate(
            tomlkit.parse((output / 'build.toml').read_bytes().decode()).unwrap()
        )
        for name in (artifacts.image, artifacts.target):
            path = (output / name).resolve()
            if (
                Path(name).is_absolute()
                or not path.is_relative_to(output)
                or not path.is_file()
            ):
                raise target_process.TargetAdapterError(
                    f'{name}: build artifact must be a file inside the output directory'
                )
    except (OSError, ParseError, UnicodeDecodeError, ValidationError) as error:
        raise target_process.TargetAdapterError(str(error)) from error
    return artifacts


def run(command: Build) -> Artifacts:
    executable = target_process.entry_point(
        command.target_dir, 'build_entry.py', 'a build entry point'
    )
    try:
        output = command.output.resolve()
        source_root = command.source_root.resolve()
        temporary = output / 'tmp'
        temporary.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        raise target_process.TargetAdapterError(str(error)) from error
    environment = dict(
        os.environ,
        TMPDIR=str(temporary),
        TMP=str(temporary),
        TEMP=str(temporary),
        PYTHONPATH=str(source_root)
        + (
            os.pathsep + os.environ['PYTHONPATH']
            if os.environ.get('PYTHONPATH')
            else ''
        ),
    )
    target_process.run(
        [
            sys.executable,
            str(executable),
            '--source-root',
            str(source_root),
            '--output',
            str(output),
            *(['--diagnostics'] if command.diagnostics else []),
            *(['--publish'] if command.publish else []),
        ],
        check=True,
        env=environment,
    )
    return read_artifacts(output)
