"""Back up the MC6 Pro, upload its built image, restore and verify saved data."""

import time
from pathlib import Path

import tyro
from pydantic import BaseModel

from ....firmware.intel_hex import read_hex
from ....patch_switcher import backups, devices, songs
from ....upload import Upload
from . import legacy_usb, upload_helper


class Flash(BaseModel, frozen=True):
    directory: Path
    port: Path
    legacy_usb: bool = False


def run(command: Flash) -> None:
    directory = command.directory.resolve()
    source = directory / 'firmware.hex'
    image = read_hex(source)
    upload_helper.validate_firmware(image)
    if not (upload_helper.CACHE_DIRECTORY / 'authentication.key').is_file():
        raise ValueError('start the upload helper in Terminal before flashing')
    matches = [
        d
        for d in devices.connected()
        if d.serial == upload_helper.BOARD_SERIAL and d.port == command.port
    ]
    if len(matches) != 1:
        raise ValueError('the selected port is not the expected MC6 Pro')
    before = directory / 'before'
    backups.backup_pedal(
        before,
        command.port,
        legacy_usb.Connection(command.port, timeout=5) if command.legacy_usb else None,
    )
    backups.read_setup_binaries(before)
    backups.read_history_binaries(before)
    songs.decode((before / 'songs.bin').read_bytes())
    print(f'Backup validated. Uploading SHA-256 {image.sha256}', flush=True)
    upload_helper.request_bootloader()
    deadline = time.monotonic() + 10
    while any(d.serial == upload_helper.BOARD_SERIAL for d in devices.connected()):
        if time.monotonic() >= deadline:
            raise TimeoutError('pedal did not leave application USB mode')
        time.sleep(0.1)
    result = upload_helper.request_upload(Upload(image=source, sha256=image.sha256))
    print(result.output, end='', flush=True)
    deadline = time.monotonic() + 30
    while not any(
        d.serial == upload_helper.BOARD_SERIAL and d.port == command.port
        for d in devices.connected()
    ):
        if time.monotonic() >= deadline:
            raise TimeoutError(
                'pedal did not return after upload '
                f'(uploader status {result.returncode}); '
                f'backup retained at {before}'
            )
        time.sleep(0.1)
    backups.restore_setups(before, command.port)
    backups.verify_after_flashing(before, command.port)
    if result.returncode:
        raise ValueError(
            f'device data restored and verified, but uploader returned '
            f'{result.returncode}; firmware installation is not confirmed'
        )
    print(f'Flashed and verified SHA-256 {image.sha256}', flush=True)


def main() -> None:
    run(tyro.cli(Flash))


if __name__ == '__main__':
    main()
