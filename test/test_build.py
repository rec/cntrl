"""The implementation build protocol uses an executable and relocatable files."""

import io
import os
import shutil
import subprocess
from pathlib import Path

import pytest
import tomlkit

from cntrl import build


@pytest.mark.parametrize('diagnostics', [False, True])
def test_build_exports_survive_relocation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, diagnostics: bool
) -> None:
    monkeypatch.setattr(
        io, 'text_encoding', lambda encoding, *args: encoding or 'cp1252'
    )
    adapter = Path(__file__).parent / 'fixtures/build_target'
    output = tmp_path / 'output with spaces'
    inherited_temporary = str(tmp_path / 'outside output')
    for name in ('TMPDIR', 'TMP', 'TEMP'):
        monkeypatch.setenv(name, inherited_temporary)
    artifacts = build.run(
        build.Build(target_dir=adapter, output=output, diagnostics=diagnostics)
    )
    assert (
        tomlkit.parse((output / 'build.toml').read_bytes().decode()).unwrap()[
            'diagnostics'
        ]
        is diagnostics
    )
    temporary_file = Path((output / 'temporary-path.txt').read_bytes().decode().strip())
    assert temporary_file.parent == output / 'tmp'
    assert temporary_file.read_text() == 'temporary build data\n'
    for name in ('TMPDIR', 'TMP', 'TEMP'):
        assert os.environ[name] == inherited_temporary
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
