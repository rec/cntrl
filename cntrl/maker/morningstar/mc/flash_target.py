"""Back up the MC6 Pro, upload its built image, restore and verify saved data."""

import hashlib
import shutil
import subprocess
import sys
import time
from importlib.metadata import version
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel, Field

from ....firmware.intel_hex import read_hex
from ....patch_switcher import (
    backups,
    data_files,
    devices,
    models,
    songs,
    storage_info,
    transport,
)
from ....patch_switcher.setup_sync import write_record
from ....upload import Upload
from . import legacy_usb, release, upload_helper


class Flash(BaseModel, frozen=True):
    directory: Path
    resources: Path | None = None
    port: Path | None = None
    serial: str | None = Field(default=None, pattern=r'^[0-9]+$')
    first_install: bool = False
    stock_version: str | None = None
    editor_backup: Path | None = None
    legacy_usb: bool = False
    prepare_only: bool = False


def run(command: Flash) -> None:
    directory = command.directory.resolve()
    if directory.exists():
        raise ValueError(
            f'{directory}: choose a new installation directory '
            'to preserve previous evidence'
        )
    if command.first_install:
        if command.port is not None:
            raise ValueError(
                'first install selects the stock unit with --serial, not --port'
            )
        if (
            command.stock_version != release.STOCK.version
            or command.editor_backup is None
            or not command.serial
        ):
            raise ValueError(
                'first install requires --serial, --stock-version '
                + release.STOCK.version
                + ' and --editor-backup; --first-install confirms '
                'the physical MC6 Pro model'
            )
        if not command.editor_backup.read_bytes():
            raise ValueError('editor backup is empty')
        if command.legacy_usb:
            raise ValueError(
                '--legacy-usb applies only to an existing cntrl installation'
            )
    elif command.stock_version is not None or command.editor_backup is not None:
        raise ValueError('--stock-version and --editor-backup require --first-install')
    manifest = release.prepare(
        command.resources or Path(__file__).with_name('distribution'), directory
    )
    source = directory / 'firmware.hex'
    image = read_hex(source)
    record: dict[str, object] = {
        'controller': manifest.controller,
        'package_version': version('cntrl'),
        'firmware_source_commit': manifest.source_commit,
        'stock_filename': manifest.stock.filename,
        'stock_sha256': manifest.stock.sha256,
        'components': manifest.files,
        'image_sha256': image.sha256,
        'first_install': command.first_install,
        'result': 'prepared',
    }
    write_record(directory / 'installation.toml', record)
    print(f'Prepared firmware source {manifest.source_commit}: {source}', flush=True)
    if command.prepare_only:
        return
    if not (upload_helper.CACHE_DIRECTORY / 'authentication.key').is_file():
        raise ValueError('start the upload helper in Terminal before flashing')
    errors: list[str] = []
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(name='target', paths=[directory / manifest.target]),
            True,
            errors,
        )
    )
    if errors:
        raise ValueError('\n'.join(errors))
    upload_helper.updater_path()
    try:
        if command.first_install:
            assert command.serial and command.editor_backup
            serial = command.serial
            board = upload_helper.stock_board(serial)
            ready = check_helper(serial)
            record.update(
                uploader=ready.uploader, uploader_sha256=ready.uploader_sha256
            )
            backup = directory / 'editor-backup'
            shutil.copyfile(command.editor_backup, backup)
            record.update(
                editor_backup=str(backup),
                editor_backup_source=str(command.editor_backup.resolve()),
                editor_backup_sha256=hashlib.sha256(backup.read_bytes()).hexdigest(),
            )
            write_record(directory / 'installation.toml', record)
            requested = upload_helper.request_stock_bootloader(board)
            if not requested or not wait_halfkay(serial):
                if not sys.stdin.isatty():
                    raise ValueError(
                        'automatic stock bootloader entry unavailable; '
                        'run interactively to confirm manual update mode'
                    )
                input(
                    'Automatic bootloader entry is unavailable. '
                    'Morningstar documents internal-button recovery here: '
                    'https://help.morningstar.io/en/article/'
                    'firmware-upload-failure-controller-does-not-boot-up-1b7h2fe/ '
                    '(requires removing the backplate). '
                    'Enter HalfKay recovery mode, then press Enter: '
                )
            restore = None
        else:
            port = devices.select_port(command.port)
            matches = [
                d
                for d in devices.connected()
                if d.port == port
                and d.serial
                and (command.serial is None or d.serial == command.serial)
            ]
            if len(matches) != 1:
                raise ValueError(
                    'the selected cntrl port has no unique matching unit serial'
                )
            serial = matches[0].serial
            ready = check_helper(serial)
            record.update(
                uploader=ready.uploader, uploader_sha256=ready.uploader_sha256
            )
            before = directory / 'before'
            backups.backup_pedal(
                before,
                port,
                legacy_usb.Connection(port, timeout=5) if command.legacy_usb else None,
            )
            status = (before / 'status.txt').read_text()
            fields = dict(p.split('=', 1) for p in status.split() if '=' in p)
            if fields.get('model', 'mc6-pro') != 'mc6-pro':
                raise ValueError(
                    'the connected cntrl firmware identifies a different controller'
                )
            restore = backups.prepare_restore(before, directory / 'restore', target)
            record['backup'] = str(before)
            write_record(directory / 'installation.toml', record)
            upload_helper.request_bootloader(serial)
            wait_disconnected(serial)
        record.update(serial=serial, result='uploading')
        write_record(directory / 'installation.toml', record)
        result = upload_helper.request_upload(
            Upload(image=source, sha256=image.sha256, serial=serial)
        )
        print(result.output, end='', flush=True)
        record['uploader_returncode'] = result.returncode
        record.update(uploader=result.uploader, uploader_sha256=result.uploader_sha256)
        write_record(directory / 'installation.toml', record)
        port = wait_connected(serial)
        if restore is None:
            if result.returncode:
                raise ValueError(
                    f'uploader returned {result.returncode}; '
                    'starter installation was not attempted'
                )
            install_starter(directory, port)
        else:
            backups.restore_setups(restore, port, target)
            backups.verify_after_flashing(restore, port, target)
        with transport.SerialConnection(port) as connection:
            status = transport.SetupClient(connection).request('CNTRL STATUS')
        fields = dict(p.split('=', 1) for p in status.split() if '=' in p)
        if (
            fields.get('git') != manifest.source_commit[:7]
            or fields.get('model') != 'mc6-pro'
        ):
            raise ValueError(
                f'running firmware identity differs from packaged release: {status}'
            )
        if result.returncode:
            raise ValueError(
                f'data restored, but uploader returned {result.returncode}; '
                'installation is not confirmed'
            )
        record.update(result='verified', status=status)
        write_record(directory / 'installation.toml', record)
        print(f'Flashed and verified SHA-256 {image.sha256}', flush=True)
    except (
        OSError,
        ValueError,
        subprocess.CalledProcessError,
        EOFError,
        KeyboardInterrupt,
    ) as error:
        record.update(result='failed', error=str(error))
        write_record(directory / 'installation.toml', record)
        raise


def check_helper(serial: str) -> upload_helper.UploadResult:
    result = upload_helper.request_upload(upload_helper.Check(serial=serial))
    if result.returncode:
        raise ValueError(result.output)
    return result


def wait_disconnected(serial: str) -> None:
    deadline = time.monotonic() + 10
    while any(d.serial == serial for d in devices.connected()):
        if time.monotonic() >= deadline:
            raise TimeoutError('pedal did not leave application USB mode')
        time.sleep(0.1)


def wait_connected(serial: str) -> Path:
    deadline = time.monotonic() + 30
    while True:
        matches = [d for d in devices.connected() if d.serial == serial]
        if len(matches) == 1:
            return matches[0].port
        if time.monotonic() >= deadline:
            raise TimeoutError(
                'pedal did not return after upload; '
                'installation evidence and backups retained'
            )
        time.sleep(0.1)


def wait_halfkay(serial: str) -> bool:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if any(
            b.serial == serial
            and b.description == 'HalfKay'
            and b.model == 'Teensy 4.1'
            and 'upload' in b.capabilities
            for b in upload_helper.list_boards()
        ):
            return True
        time.sleep(0.1)
    return False


def install_starter(directory: Path, port: Path) -> None:
    """Initialize an empty pedal through ordinary transactional deployment."""
    before = directory / 'before-starter'
    backups.backup_pedal(before, port)
    backups.check_conversion(before)
    report = storage_info.StorageInfo.model_validate(
        tomlkit.parse((before / 'storage.toml').read_text()).unwrap()
    )
    status = (before / 'status.txt').read_text()
    if (
        (before / 'catalogue.txt').read_text().strip()
        or ' persistent_root=0 ' not in status + ' '
        or backups.read_history_binaries(before)
        or any(
            e.path.startswith('/cntrl/generations/')
            and not e.path.startswith('/cntrl/generations/00000000')
            for e in report.entries
        )
    ):
        raise ValueError('existing cntrl data found; starter refused, backup retained')
    starter = directory / 'starter'
    bundle = (starter / 'bundle.bin').read_bytes()
    library = (starter / 'songs.bin').read_bytes()
    fonts = sorted((starter / 'fonts').glob('*.cfont'))
    backups.check_deployment_space(
        report,
        [len(bundle) + 100] * 2
        + [len(library) + 28] * 2
        + [p.stat().st_size for p in fonts]
        + [96, 96, 24, 24],
        20,
    )
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        if not client.recover_deployment() or client.catalogue():
            raise ValueError('starter requires an empty, ready collection')
        for path in fonts:
            client.copy_font(int(path.stem, 16), path.read_bytes())
        client.begin_deployment()
        client.copy_bundle(bundle, overwrite=True)
        songs.upload(client, library, overwrite=True)
        client.ok('CNTRL SETUP FINISH')
        client.select('starter')
        if client.current() != 'starter':
            raise ValueError('starter selection readback failed')
        client.commit_deployment(bundle, library)
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        if (
            client.download_bundle() != bundle
            or songs.download(client) != library
            or client.current() != 'starter'
        ):
            raise ValueError('starter deployment verification failed')


def main() -> None:
    run(tyro.cli(Flash))


if __name__ == '__main__':
    main()
