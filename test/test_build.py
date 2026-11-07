"""The implementation build protocol uses an executable and relocatable files."""

import shutil
import subprocess
from pathlib import Path

import pytest

from cntrl import build


def test_build_exports_survive_relocation(tmp_path: Path) -> None:
    adapter = Path(__file__).parent / 'fixtures/build_target'
    output = tmp_path / 'output with spaces'
    artifacts = build.run(build.Build(target_dir=adapter, output=output))
    relocated = tmp_path / 'relocated'
    shutil.move(output, relocated)
    assert (relocated / artifacts.image).read_text() == 'test image\n'
    assert (relocated / artifacts.target).read_text() == "buttons = ['one']\n"


def test_build_failure_propagates(tmp_path: Path) -> None:
    adapter = Path(__file__).parent / 'fixtures/build_target'
    with pytest.raises(subprocess.CalledProcessError):
        build.run(
            build.Build(
                target_dir=adapter, output=tmp_path / 'out', source_root=tmp_path
            )
        )
