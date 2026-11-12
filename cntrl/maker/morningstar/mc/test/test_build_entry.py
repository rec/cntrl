"""An absolute target directory selects its own source, independently of cwd."""

import os
import subprocess
import sys
from pathlib import Path


def test_adapter_entry_binds_selected_helpers(tmp_path: Path) -> None:
    directory = Path(__file__).resolve().parents[1]
    source_root = directory.parents[3]
    environment = dict(
        os.environ, CNTRL_PYTHON=sys.executable, PYTHONPATH=str(source_root)
    )
    subprocess.run(
        [sys.executable, str(directory / 'test/build_entry_peer.py'), str(directory)],
        cwd=tmp_path,
        env=environment,
        check=True,
    )
    result = subprocess.run(
        [sys.executable, str(directory / 'build_entry.py'), '--help'],
        cwd=tmp_path,
        env=environment,
        capture_output=True,
        text=True,
        check=True,
    )
    assert '--source-root' in result.stdout
    assert '--output' in result.stdout
    assert '--diagnostics' in result.stdout
    result = subprocess.run(
        [sys.executable, str(directory / 'upload_helper_entry.py'), '--help'],
        cwd=tmp_path,
        env=environment,
        capture_output=True,
        text=True,
        check=True,
    )
    assert 'serve' in result.stdout
    assert 'upload' in result.stdout
    assert 'bootloader' in result.stdout
