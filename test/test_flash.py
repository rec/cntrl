"""Flash dispatch uses the selected target and fails before building without it."""

import shutil
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
    assert (
        output / 'flashed.txt'
    ).read_bytes() == f'{Path("/unused")}\nTrue\n'.encode()


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
            '--skip-build',
        ],
    )
    assert command.target_dir == Path('adapter') and command.output == Path('out')
    assert command.port == Path('/unused') and command.legacy_usb
    assert command.skip_build


def test_existing_export_uses_the_selected_adapter_without_rebuilding(
    tmp_path: Path,
) -> None:
    adapter = tmp_path / 'current adapter'
    adapter.mkdir()
    shutil.copyfile(
        Path(__file__).parent / 'fixtures/build_target/flash_entry.py',
        adapter / 'flash_entry.py',
    )
    output = tmp_path / 'historical export'
    output.mkdir()
    (output / 'build.toml').write_text(
        "image = 'firmware.bin'\ntarget = 'target.toml'\n"
    )
    (output / 'firmware.bin').write_bytes(b'historical image\n')
    (output / 'target.toml').write_text("buttons = ['one']\n")
    flash.run(
        flash.Flash(
            target_dir=adapter, output=output, port=Path('/unused'), skip_build=True
        )
    )
    assert (output / 'firmware.bin').read_bytes() == b'historical image\n'
    assert (
        output / 'flashed.txt'
    ).read_bytes() == f'{Path("/unused")}\nFalse\n'.encode()
    assert (output / 'tmp').is_dir()


@pytest.mark.parametrize('artifact', ['missing.bin', '../outside.bin', '.'])
def test_existing_export_rejects_invalid_artifacts_before_flashing(
    tmp_path: Path, artifact: str
) -> None:
    adapter = Path(__file__).parent / 'fixtures/build_target'
    output = tmp_path / 'export'
    output.mkdir()
    (tmp_path / 'outside.bin').write_bytes(b'outside\n')
    (output / 'build.toml').write_text(
        f"image = '{artifact}'\ntarget = 'target.toml'\n"
    )
    (output / 'target.toml').write_text("buttons = ['one']\n")
    with pytest.raises(ValueError, match='build artifact must be a file inside'):
        flash.run(
            flash.Flash(
                target_dir=adapter, output=output, port=Path('/unused'), skip_build=True
            )
        )
    assert not (output / 'flashed.txt').exists()
    assert not (output / 'tmp').exists()


def test_existing_export_requires_its_manifest_before_flashing(tmp_path: Path) -> None:
    adapter = Path(__file__).parent / 'fixtures/build_target'
    output = tmp_path / 'missing export'
    with pytest.raises(FileNotFoundError):
        flash.run(
            flash.Flash(
                target_dir=adapter, output=output, port=Path('/unused'), skip_build=True
            )
        )
    assert not output.exists()
