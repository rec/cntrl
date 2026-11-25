"""Packaged flashing dispatch never invokes the maintainer build."""

import subprocess
from pathlib import Path
from unittest.mock import Mock

import pytest
import tyro

from cntrl import flash


@pytest.mark.parametrize('helper_args', [[], ['--upload-helper']])
def test_packaged_flash_forwards_first_install_and_never_builds(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, helper_args: list[str]
) -> None:
    adapter = tmp_path / 'adapter'
    adapter.mkdir()
    (adapter / 'flash_entry.py').touch()
    monkeypatch.setattr(
        flash, 'controller_directory', lambda controller: adapter / 'distribution'
    )
    run = Mock()
    monkeypatch.setattr(subprocess, 'run', run)
    command = tyro.cli(
        flash.Flash,
        args=[
            '--controller',
            'test-pedal',
            '--serial',
            '12345',
            '--output',
            str(tmp_path / 'installation'),
            '--first-install',
            '--stock-version',
            '1.2.3',
            '--editor-backup',
            'backup.file',
            *helper_args,
        ],
    )
    flash.run(command)
    arguments = run.call_args.args[0]
    assert arguments[1] == str(adapter / 'flash_entry.py')
    assert arguments[2:] == [
        '--directory',
        str(tmp_path / 'installation'),
        '--serial',
        '12345',
        '--stock-version',
        '1.2.3',
        '--editor-backup',
        'backup.file',
        '--first-install',
        *helper_args,
    ]
    assert run.call_count == 1


def test_missing_adapter_fails_before_creating_installation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        flash, 'controller_directory', lambda controller: tmp_path / 'distribution'
    )
    output = tmp_path / 'installation'
    with pytest.raises(ValueError, match='does not provide a flash entry'):
        flash.run(flash.Flash(controller='test', output=output, prepare_only=True))
    assert not output.exists()
