"""Back up the MC6 Pro, upload its built image, restore and verify saved data."""

import hashlib
import re
import shutil
import subprocess
import sys
import time
from collections.abc import Callable
from contextlib import nullcontext
from importlib.metadata import version
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel, Field

from ....firmware.intel_hex import read_hex
from ....patch_chooser import (
    backups,
    data_files,
    devices,
    models,
    songs,
    storage_info,
    transport,
)
from ....patch_chooser.setup_sync_store import write_record
from . import legacy_usb, release, upload_helper


class Flash(BaseModel, frozen=True):
    directory: Path
    resources: Path | None = None
    doom_assets: Path | None = None
    port: Path | None = None
    serial: str | None = Field(default=None, pattern=r'^[0-9]+$')
    first_install: bool = False
    stock_version: str | None = None
    editor_backup: Path | None = None
    legacy_usb: bool = False
    prepare_only: bool = False


class FlashError(ValueError):
    """The installer could not complete the firmware flash."""


def run(
    command: Flash,
    *,
    connection_factory: Callable[[Path], transport.SerialConnection] | None = None,
) -> None:
    if connection_factory is not None and (command.first_install or command.legacy_usb):
        raise ValueError('recorded connections require an existing cntrl installation')
    directory = command.directory.resolve()
    if directory.exists():
        raise ValueError(
            f'{directory}: choose a new installation directory '
            'to preserve previous evidence'
        )
    doom_assets = command.doom_assets.read_bytes() if command.doom_assets else None
    if doom_assets is not None and len(doom_assets) != 4 * 1024 * 1024:
        raise ValueError('expected a 4 MiB Doom E1M1 asset image')
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
    if doom_assets is not None and 'doom' not in target.functions:
        raise ValueError('--doom-assets requires a Doom-enabled firmware build')
    upload_helper.updater_path()
    expected_commit = manifest.source_commit[:7]
    stock: Path | None = None
    restore: Path | None = None
    try:
        if command.first_install:
            assert command.serial and command.editor_backup
            serial = command.serial
            board = upload_helper.stock_board(serial)
            ready = check_helper(serial)
            stock = release.stock_image()
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
            requested = upload_helper.request_stock_bootloader(board, quiet=True)
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
            with (
                connection_factory(port)
                if connection_factory is not None
                else nullcontext(
                    legacy_usb.Connection(port, timeout=5)
                    if command.legacy_usb
                    else None
                )
            ) as connection:
                backups.backup_pedal(before, port, connection)
            status = (before / 'status.txt').read_text()
            fields = dict(p.split('=', 1) for p in status.split() if '=' in p)
            if fields.get('model', 'mc6-pro') != 'mc6-pro':
                raise ValueError(
                    'the connected cntrl firmware identifies a different controller'
                )
            restore = backups.prepare_restore(before, directory / 'restore', target)
            record['backup'] = str(before)
            write_record(directory / 'installation.toml', record)
            upload_helper.request_bootloader(serial, quiet=True)
            wait_disconnected(serial)
        record.update(serial=serial, result='uploading')
        write_record(directory / 'installation.toml', record)
        result, port, verify_after_restore = _install_with_recovery(
            upload_helper.ManagedUpload(
                image=source,
                sha256=image.sha256,
                serial=serial,
                first_install=command.first_install,
            ),
            stock,
            expected_commit,
            connection_factory,
        )
        record['uploader_returncode'] = result.returncode
        record.update(uploader=result.uploader, uploader_sha256=result.uploader_sha256)
        write_record(directory / 'installation.toml', record)
        if restore is None:
            install_starter(directory, port)
        else:
            with (
                connection_factory(port)
                if connection_factory is not None
                else nullcontext(None)
            ) as connection:
                backups.restore_setups(restore, port, target, connection=connection)
                backups.verify_after_flashing(
                    restore, port, target, connection=connection
                )
        status = _read_status(port, connection_factory)
        if _firmware_commit(status) != expected_commit:
            raise FlashError('CNTRL could not verify the installed firmware.')
        if doom_assets is not None:
            print('Copying and verifying Doom assets over USB', flush=True)
            connect = connection_factory or transport.SerialConnection
            with connect(port) as connection:
                transport.SetupClient(connection).copy_doom_assets(doom_assets)
        if verify_after_restore:
            upload_helper.record_reconnect(
                result,
                expected_commit,
                f'Expected build running (`{expected_commit}`) after restore',
            )
        record.update(result='verified', status=status)
        write_record(directory / 'installation.toml', record)
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


def _read_status(
    port: Path, connection_factory: Callable[[Path], transport.SerialConnection] | None
) -> str:
    if connection_factory is None:
        return transport.device_status(port)
    with connection_factory(port) as connection:
        return transport.SetupClient(connection).request('CNTRL STATUS')


def _install_with_recovery(
    image: upload_helper.ManagedUpload,
    stock: Path | None,
    expected: str,
    connection_factory: Callable[[Path], transport.SerialConnection] | None,
) -> tuple[upload_helper.UploadResult, Path, bool]:
    attempted = False
    for delay in (0, *RETRY_DELAYS):
        if delay:
            time.sleep(delay)
            if not _recover_halfkay(image.serial):
                continue
        result = upload_helper.request_upload(image)
        attempted = attempted or result.attempt is not None
        port, outcome = _observe_installation(
            image, expected, result, connection_factory
        )
        upload_helper.record_reconnect(result, expected, outcome)
        if port is not None:
            return result, port, outcome == LEGACY_STORAGE_RECOVERY

    if not attempted:
        raise FlashError(
            'CNTRL could not start a firmware upload. Check the USB connection '
            'and the running upload helper. The installer did not start a flash.'
        )

    if stock is None:
        raise FlashError(
            'CNTRL could not verify the firmware update. Your setup and history backup '
            f'is saved in {image.image.parent / "before"}.'
        )

    rollback = image.model_copy(update={'image': stock, 'sha256': release.STOCK.sha256})
    stock_version = f'Morningstar stock {release.STOCK.version}'
    for delay in RETRY_DELAYS:
        time.sleep(delay)
        if not _recover_halfkay(image.serial):
            continue
        result = upload_helper.request_upload(rollback)
        _, outcome = _observe_installation(rollback, None, result, None)
        upload_helper.record_reconnect(result, stock_version, outcome)
        if result.returncode == 0 and outcome == 'Morningstar stock running':
            raise FlashError(f'CNTRL was not installed. {stock_version} was restored.')

    raise FlashError(
        'CNTRL could not install or restore Morningstar firmware automatically. '
        'Follow Morningstar’s instructions to remove the backplate. Connect the '
        'pedal directly by USB, open the Morningstar Firmware Updater, and briefly '
        'press the internal PROGRAM button. Select the saved Morningstar HEX to flash. '
        'Your editor backup is saved in the installation directory.\n'
        'https://help.morningstar.io/en/article/'
        'firmware-upload-failure-controller-does-not-boot-up-1b7h2fe/\n'
        f'Morningstar HEX: {stock}'
    )


def _recover_halfkay(serial: str) -> bool:
    try:
        boards = [b for b in upload_helper.list_boards() if b.serial == serial]
        if len(boards) == 1:
            board = boards[0]
            if _is_halfkay(board, serial):
                return True
            if board.description == 'Morningstar MC6 Pro':
                if not upload_helper.request_stock_bootloader(board, quiet=True):
                    return False
            elif any(d.serial == serial for d in devices.connected()):
                upload_helper.request_bootloader(serial, quiet=True)
        return wait_halfkay(serial)
    except (OSError, ValueError, subprocess.CalledProcessError):
        return False


def _observe_installation(
    image: upload_helper.ManagedUpload,
    expected: str | None,
    result: upload_helper.UploadResult,
    connection_factory: Callable[[Path], transport.SerialConnection] | None,
) -> tuple[Path | None, str]:
    serial = image.serial
    deadline = time.monotonic() + 30
    outcome = 'Pedal did not reconnect before timeout'
    while True:
        try:
            ports = [d.port for d in devices.connected() if d.serial == serial]
            if len(ports) == 1:
                try:
                    status = _read_status(ports[0], connection_factory)
                except transport.UsbReplyError as error:
                    status = str(error)
                    if status != STORAGE_RECOVERY and not status.startswith(
                        STORAGE_RECOVERY + ' '
                    ):
                        raise
                commit = _firmware_commit(status)
                if commit == expected and commit is not None:
                    observation = f'Expected build running (`{commit}`)'
                    if status.startswith(STORAGE_RECOVERY):
                        observation += ' in storage recovery'
                    return ports[0], observation
                if commit is not None:
                    return None, f'Different firmware running (`{commit}`)'
                if (
                    not image.first_install
                    and status == STORAGE_RECOVERY
                    and result.returncode == 0
                ):
                    return ports[0], LEGACY_STORAGE_RECOVERY
                outcome = 'CNTRL reconnected, firmware unverified'
            boards = [b for b in upload_helper.list_boards() if b.serial == serial]
            if len(boards) == 1:
                if boards[0].description == 'Morningstar MC6 Pro':
                    return None, (
                        'Morningstar stock running'
                        if expected is None
                        else 'Different firmware running (Morningstar stock)'
                    )
                if _is_halfkay(boards[0], serial):
                    outcome = 'Pedal stayed in HalfKay'
                    if result.returncode and 'I/O error while writing' in result.output:
                        # The confirmed bootloader is safe to retry. Running a
                        # partially written application could strand the pedal.
                        return None, outcome
        except (
            *transport.USB_ERRORS,
            OSError,
            EOFError,
            subprocess.CalledProcessError,
        ):
            outcome = 'Reconnect could not be verified'
        if time.monotonic() >= deadline:
            return None, outcome
        time.sleep(0.1)


def _firmware_commit(status: str) -> str | None:
    fields = dict(p.split('=', 1) for p in status.split() if '=' in p)
    commit = fields.get('git', '')
    if (
        status.startswith(('CNTRL READY ', STORAGE_RECOVERY + ' '))
        and fields.get('model') == 'mc6-pro'
        and re.fullmatch('[0-9a-f]{7}', commit)
    ):
        return commit
    return None


def check_helper(serial: str) -> upload_helper.UploadResult:
    result = upload_helper.request_upload(upload_helper.Check(serial=serial))
    if result.returncode:
        raise ValueError(result.output)
    if not result.managed_recovery:
        raise FlashError('Restart the CNTRL upload helper before flashing.')
    return result


def wait_disconnected(serial: str) -> None:
    deadline = time.monotonic() + 10
    while any(d.serial == serial for d in devices.connected()):
        if time.monotonic() >= deadline:
            raise TimeoutError('pedal did not leave application USB mode')
        time.sleep(0.1)


def wait_halfkay(serial: str) -> bool:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if any(_is_halfkay(b, serial) for b in upload_helper.list_boards()):
            return True
        time.sleep(0.1)
    return False


def _is_halfkay(board: upload_helper.Board, serial: str) -> bool:
    return (
        board.serial == serial
        and board.description == 'HalfKay'
        and board.model == 'Teensy 4.1'
        and 'upload' in board.capabilities
    )


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
    language_packs = sorted((directory / 'languages').glob('*.bin'))
    backups.check_deployment_space(
        report,
        [len(bundle) + 100] * 2
        + [len(library) + 28] * 2
        + [p.stat().st_size for p in fonts]
        + [p.stat().st_size for p in language_packs]
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
        for path in language_packs:
            client.copy_language(path.stem, path.read_bytes())
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
    try:
        run(tyro.cli(Flash))
    except FlashError as error:
        sys.exit(str(error))


RETRY_DELAYS = (0.1, 0.2, 0.3, 1, 2)
STORAGE_RECOVERY = 'CNTRL ERROR deployment-recovery'
LEGACY_STORAGE_RECOVERY = (
    'CNTRL reconnected in storage recovery; checking build after restore'
)


if __name__ == '__main__':
    main()
