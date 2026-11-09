"""A minimal target implementing the interpreter-based build contract."""

import os
from pathlib import Path
from tempfile import NamedTemporaryFile

import tyro
from pydantic import BaseModel


class Build(BaseModel, frozen=True):
    source_root: Path
    output: Path


def run(command: Build) -> None:
    assert (command.source_root / 'firmware/include/cntrl').is_dir()
    assert all(
        Path(os.environ[n]) == command.output / 'tmp' for n in ('TMPDIR', 'TMP', 'TEMP')
    )
    with NamedTemporaryFile(dir=command.output / 'tmp', delete=False) as temporary:
        temporary.write(b'temporary build data\n')
    (command.output / 'temporary-path.txt').write_text(temporary.name)
    (command.output / 'build.toml').write_text(
        "image = 'firmware.bin'\ntarget = 'target.toml'\n"
    )
    (command.output / 'firmware.bin').write_text('test image\n')
    (command.output / 'target.toml').write_text("buttons = ['one']\n")


if __name__ == '__main__':
    run(tyro.cli(Build))
