"""Asset commands report input and destination failures at their boundaries."""

import sys
from pathlib import Path
from unittest.mock import Mock

import pytest

from cntrl.localization import build
from cntrl.maker.morningstar.mc import font_coverage
from cntrl.patch_chooser import cli, devices


def test_missing_language_pack_is_reported_before_usb(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    select_port = Mock(side_effect=AssertionError('unexpected device selection'))
    monkeypatch.setattr(devices, 'select_port', select_port)
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'language', 'install', 'zz'])

    with pytest.raises(SystemExit, match=r'zz\.bin'):
        cli.main()

    select_port.assert_not_called()


def test_font_report_names_unwritable_destination(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(font_coverage, 'generate', lambda: 'Report\n')
    destination = tmp_path / 'missing' / 'fonts.md'
    monkeypatch.setattr(
        sys, 'argv', ['cntrl', 'font-coverage', '--output', str(destination)]
    )

    with pytest.raises(SystemExit, match='fonts.md'):
        cli.main()


@pytest.mark.parametrize('error_type', [ValueError, OSError])
@pytest.mark.parametrize('command', ['font-coverage', 'language build'])
def test_asset_commands_preserve_unexpected_failures(
    monkeypatch: pytest.MonkeyPatch, command: str, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected asset generation failure')
    failing_generator = Mock(side_effect=failure)
    if command == 'font-coverage':
        monkeypatch.setattr(font_coverage, 'generate', failing_generator)
    else:
        monkeypatch.setattr(build, 'build', failing_generator)
    monkeypatch.setattr(sys, 'argv', ['cntrl', *command.split()])

    with pytest.raises(error_type) as caught:
        cli.main()

    assert caught.value is failure
