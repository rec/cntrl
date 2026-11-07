"""Build an explicitly selected implementation through its executable contract."""

import os
import subprocess
import sys
from pathlib import Path

import tomlkit
from pydantic import BaseModel, Field


class Build(BaseModel, frozen=True):
    target_dir: Path
    output: Path
    source_root: Path = Field(default_factory=Path.cwd)


class Artifacts(BaseModel, frozen=True):
    """Paths relative to the build output, retained when it is relocated."""

    image: str
    target: str


def run(command: Build) -> Artifacts:
    output = command.output.resolve()
    executable = command.target_dir.resolve() / 'build'
    environment = dict(
        os.environ,
        CNTRL_PYTHON=sys.executable,
        PYTHONPATH=str(command.source_root.resolve())
        + (
            os.pathsep + os.environ['PYTHONPATH']
            if os.environ.get('PYTHONPATH')
            else ''
        ),
    )
    subprocess.run(
        [
            str(executable),
            '--source-root',
            str(command.source_root.resolve()),
            '--output',
            str(output),
        ],
        check=True,
        env=environment,
    )
    artifacts = Artifacts.model_validate(
        tomlkit.parse((output / 'build.toml').read_text()).unwrap()
    )
    for name in (artifacts.image, artifacts.target):
        path = (output / name).resolve()
        if (
            Path(name).is_absolute()
            or not path.is_relative_to(output)
            or not path.is_file()
        ):
            raise ValueError(
                f'{name}: build artifact must be a file inside the output directory'
            )
    return artifacts
