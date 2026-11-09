"""Flash dispatch uses the selected target and fails before building without it."""

from pathlib import Path

import pytest
import tyro

from cntrl import flash


def test_flash_requires_a_target_entry_before_building(tmp_path: Path) -> None:
    output = tmp_path / 'output'
    with pytest.raises(ValueError, match='does not provide a flash entry'):
        flash.run(flash.Flash(target_dir=tmp_path, output=output, port=Path('/unused')))
    assert not output.exists()


def test_selected_target_receives_the_completed_export(tmp_path: Path) -> None:
    adapter = Path(__file__).parent / 'fixtures/build_target'
    output = tmp_path / 'flash output'
    flash.run(
        flash.Flash(
            target_dir=adapter,
            output=output,
            port=Path('/unused'),
            legacy_usb=True,
        )
    )
    assert (output / 'firmware.bin').read_bytes() == b'test image\n'
    assert (output / 'flashed.txt').read_bytes() == b'/unused\nTrue\n'


def test_flash_options_select_a_target_and_explicit_legacy_usb() -> None:
    command = tyro.cli(
        flash.Flash,
        args=[
            '--target-dir',
            'adapter',
            '--output',
            'out',
            '--port',
            '/unused',
            '--legacy-usb',
        ],
    )
    assert command.target_dir == Path('adapter') and command.output == Path('out')
    assert command.port == Path('/unused') and command.legacy_usb
