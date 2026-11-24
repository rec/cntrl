"""A manually launched, single-pedal upload service over authenticated local IPC."""

import hashlib
import os
import re
import stat
import struct
import subprocess
import sys
from datetime import UTC, datetime
from multiprocessing.connection import Client, Listener
from multiprocessing.context import AuthenticationError
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Literal
from uuid import UUID, uuid4

import tyro
from pydantic import BaseModel, TypeAdapter
from serial import Serial
from serial.tools.list_ports import comports

from ....firmware.distribution import cache_directory
from ....firmware.intel_hex import FirmwareImage, read_hex
from ....patch_chooser.devices import connected
from ....upload import Bootloader, Provision, Serve, Upload
from . import updater_provision


class FlashAttempt(BaseModel, frozen=True):
    id: UUID
    record: Path


class UploadResult(BaseModel, frozen=True):
    returncode: int
    output: str
    uploader: str = ''
    uploader_sha256: str = ''
    attempt: FlashAttempt | None = None


class Board(BaseModel, frozen=True):
    tag: str
    serial: str
    description: str
    model: str
    capabilities: list[str]


class Check(BaseModel, frozen=True):
    serial: str
    check: Literal[True] = True

    model_config = {'extra': 'forbid'}


def request_bootloader(serial: str) -> None:
    """Select the Teensy USB core's special 134-baud bootloader request."""
    ports = [d.port for d in connected() if d.serial == serial]
    if len(ports) != 1:
        raise ValueError('the expected pedal is not uniquely present in serial mode')
    with Serial(str(ports[0]), baudrate=134, timeout=5, write_timeout=5):
        pass
    # Do not restore line coding: USB must receive the reboot request.
    print('Bootloader requested; verify HalfKay enumeration before uploading.')


def list_boards() -> list[Board]:
    result = subprocess.run(
        [str(updater_path()), 'list', '--verbose', '--output', 'json'],
        capture_output=True,
        text=True,
        check=True,
    )
    return TypeAdapter(list[Board]).validate_json(result.stdout)


def stock_board(serial: str) -> Board:
    matches = [
        b
        for b in list_boards()
        if b.serial == serial and b.description == 'Morningstar MC6 Pro'
    ]
    if len(matches) != 1:
        raise ValueError(
            'the selected unit is not uniquely identified as Morningstar MC6 Pro'
        )
    return matches[0]


def request_stock_bootloader(board: Board) -> bool:
    """Try the Teensy CDC request on the pinned stock USB identity."""
    ports = [
        p.device
        for p in comports()
        if p.vid == 0x331B
        and p.pid == 7
        and p.product == 'Morningstar MC6 Pro'
        and p.serial_number == board.serial
    ]
    if len(ports) == 1:
        with Serial(ports[0], baudrate=134, timeout=5, write_timeout=5):
            pass
        return True
    if 'reboot' in board.capabilities:
        subprocess.run(
            [str(updater_path()), 'reset', '--bootloader', '--board', board.tag],
            check=True,
        )
        return True
    return False


def validate_firmware(
    image: FirmwareImage, *, origin: int = 0x60000000, end: int = 0x607C0000
) -> None:
    """Check the target's boot-image envelope, not the safety of its instructions."""
    if (
        image.origin != origin
        or image.entry != origin + 0x1000
        or len(image.regions) != 1
        or not 0x2000 <= len(image.data) <= end - origin
        or len(image.data) % 1024
    ):
        raise ValueError('expected a contiguous Teensy 4.1 application flash image')
    if hashlib.sha256(image.data[:512]).hexdigest() != FLASH_CONFIG_SHA256:
        raise ValueError('flash configuration does not match the verified MC6 Pro')
    header, reset, reserved, dcd, boot, self_address, csf, reserved2 = (
        struct.unpack_from('<8I', image.data, 0x1000)
    )
    if (
        header != 0x432000D1
        or reserved != 0
        or reserved2 != 0
        or dcd != 0
        or boot != origin + 0x1020
        or self_address != origin + 0x1000
        or not reset & 1
        or not origin + 0x1400 <= reset - 1 < csf
        or csf != image.origin + len(image.data) - 3072
        or image.data[-3072:] != bytes(3072)
        or struct.unpack_from('<3I', image.data, 0x1020)
        # The Teensy SDK's BootData names the physical flash base even when
        # the boot selector jumps to an independently linked reset handler.
        != (0x60000000, len(image.data), 0)
    ):
        raise ValueError('unexpected boot header, reset entry, boot data or CSF')


def updater_path() -> Path:
    """Use the explicit updater, or the checksum-pinned private installation."""
    if executable := os.environ.get('CNTRL_TYCMD'):
        return Path(executable)
    return updater_provision.installed(CACHE_DIRECTORY / 'tytools-0.9.9')


def flash_record_path() -> Path:
    """Use the checkout's record when the helper runs inside that checkout."""
    cwd = Path.cwd()
    for directory in (cwd, *cwd.parents):
        record = directory / 'doc' / 'flash-record.md'
        if record.is_file() and (directory / 'cntrl' / 'upload.py').is_file():
            return record
    return Path.home() / '.cntrl' / 'doc' / 'flash-record.md'


def record_reconnect(result: UploadResult, expected: str, outcome: str) -> None:
    """Append the installer's boot observation to the matching upload record."""
    if result.attempt is None:
        return
    attempt = result.attempt
    checked = datetime.now(UTC).isoformat(timespec='seconds')
    # A different upload may already have appended an entry. Link by attempt
    # ID instead of editing the file or assuming this upload is still last.
    _record_outcome(
        attempt.record,
        f'\n### Reconnect {checked}\n\n'
        f'- [Upload attempt](#attempt-{attempt.id.hex}), '
        f'uploader exit {result.returncode}\n'
        f'- Expected build `{expected}`\n'
        f'- {outcome}\n',
    )


def upload_image(request: Upload) -> UploadResult:
    """Snapshot an owned image, validate it, then upload to the selected target."""
    if not request.image.is_absolute():
        raise ValueError('firmware image path must be absolute')
    source = request.image
    metadata = source.lstat()
    if source.suffix != '.hex' or not stat.S_ISREG(metadata.st_mode):
        raise ValueError('expected a regular, non-symlink HEX file')
    if os.name != 'nt' and (metadata.st_uid != os.getuid() or metadata.st_mode & 0o022):
        raise ValueError(
            'firmware must be owned by the current user and not publicly writable'
        )
    CACHE_DIRECTORY.mkdir(mode=0o700, parents=True, exist_ok=True)
    with TemporaryDirectory(prefix='upload-', dir=CACHE_DIRECTORY) as directory:
        snapshot = Path(directory) / 'patch-chooser.hex'
        snapshot.write_bytes(source.read_bytes())
        image = read_hex(snapshot)
        if image.sha256 != request.sha256:
            raise ValueError('firmware SHA-256 changed; no upload was attempted')
        validate_firmware(image)
        updater = str(updater_path())
        listing = subprocess.run(
            [updater, 'list', '--verbose', '--output', 'json'],
            capture_output=True,
            text=True,
            check=False,
        )
        if listing.returncode:
            return UploadResult(
                returncode=listing.returncode, output=listing.stdout + listing.stderr
            )
        boards = TypeAdapter(list[Board]).validate_json(listing.stdout)
        matches = [
            b
            for b in boards
            if b.serial == request.serial
            and b.description == 'HalfKay'
            and b.model == 'Teensy 4.1'
            and 'upload' in b.capabilities
        ]
        if len(matches) != 1:
            raise ValueError(
                'the expected pedal is not uniquely present in HalfKay mode'
            )
        board = matches[0]
        print(f'Uploading SHA-256 {image.sha256} to {board.tag}', flush=True)
        attempt = FlashAttempt(id=uuid4(), record=flash_record_path())
        record = attempt.record
        started = datetime.now(UTC).isoformat(timespec='seconds')
        _append_flash_record(
            record,
            f'\n## {started}\n\n<a id="attempt-{attempt.id.hex}"></a>\n\n'
            f'- Image SHA-256 `{image.sha256}`\n',
        )
        try:
            result = subprocess.run(
                [updater, 'upload', '--board', board.tag, str(snapshot)],
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError as error:
            _record_outcome(record, f'- Failed to start uploader: {error.strerror}\n')
            raise
        output = result.stdout + result.stderr
        if result.returncode:
            diagnostic = '\n'.join((result.stdout.rstrip(), result.stderr.rstrip()))
            diagnostic = (
                diagnostic.replace(board.serial, '<serial>')
                .replace(str(snapshot), snapshot.name)
                .replace(updater, Path(updater).name)
            )
            diagnostic = re.sub(r"""IOService:/[^'"\n]*""", 'IOService:/…', diagnostic)
            details = ''.join(
                f'      {line}\n' for line in diagnostic.strip().splitlines()
            )
            _record_outcome(
                record, f'- Failed (uploader exit {result.returncode})\n\n{details}'
            )
        else:
            _record_outcome(record, '- Succeeded (uploader exit 0)\n')
        print(output, end='', flush=True)
        return UploadResult(
            returncode=result.returncode,
            output=output,
            uploader=updater,
            uploader_sha256=hashlib.sha256(Path(updater).read_bytes()).hexdigest(),
            attempt=attempt,
        )


def _append_flash_record(path: Path, entry: str) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with path.open('a', encoding='utf-8') as stream:
        if stream.tell() == 0:
            stream.write('# Flash record\n')
        stream.write(entry)
        stream.flush()
        os.fsync(stream.fileno())


def _record_outcome(path: Path, entry: str) -> None:
    try:
        _append_flash_record(path, entry)
    except OSError as error:
        # A logging failure must not turn a successful upload into a failed one.
        print(
            f'Could not finish the flash record: {error}', file=sys.stderr, flush=True
        )


def process_request(line: bytes, serial: str) -> UploadResult:
    """Accept one bounded JSON object containing an image path and digest."""
    try:
        if len(line) > REQUEST_LIMIT or not line.endswith(b'\n'):
            raise ValueError(
                f'expected a newline-terminated request (maximum {REQUEST_LIMIT} bytes)'
            )
        request = TypeAdapter(Upload | Check).validate_json(line)
        if request.serial != serial:
            raise ValueError(
                'request serial differs from the unit selected when starting the helper'
            )
        if isinstance(request, Check):
            updater = updater_path()
            return UploadResult(
                returncode=0,
                output='Upload helper ready',
                uploader=str(updater),
                uploader_sha256=hashlib.sha256(updater.read_bytes()).hexdigest(),
            )
        return upload_image(request)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        return UploadResult(returncode=1, output=f'Upload helper: {error}\n')


def serve(serial: str) -> None:
    """Handle one upload at a time; never replace an existing socket."""
    updater_path()
    if os.name != 'nt':
        os.umask(0o077)
    with Listener(
        str(SOCKET_PATH), family=IPC_FAMILY, authkey=authentication_key()
    ) as listener:
        try:
            print(f'Upload-only helper listening on {SOCKET_PATH}', flush=True)
            print(f'Flash record: {flash_record_path()}', flush=True)
            print(
                f'Selected unit: {serial}. Enter HalfKay before uploading. '
                'Ctrl-C stops the helper.'
            )
            while True:
                try:
                    with listener.accept() as connection:
                        if connection.poll(5):
                            result = process_request(
                                connection.recv_bytes(REQUEST_LIMIT), serial
                            )
                            connection.send_bytes(result.model_dump_json().encode())
                except (OSError, EOFError, AuthenticationError) as error:
                    # A disconnected client must not cancel an upload in progress.
                    print(f'Upload client disconnected: {error}', file=sys.stderr)
        finally:
            print('Upload helper stopped.', flush=True)


def request_upload(request: Upload | Check) -> UploadResult:
    with Client(
        str(SOCKET_PATH),
        family=IPC_FAMILY,
        authkey=(CACHE_DIRECTORY / 'authentication.key').read_bytes(),
    ) as connection:
        connection.send_bytes(request.model_dump_json().encode() + b'\n')
        if not connection.poll(300):
            raise TimeoutError('upload helper did not respond within 300 seconds')
        return UploadResult.model_validate_json(connection.recv_bytes(1024 * 1024))


def authentication_key() -> bytes:
    CACHE_DIRECTORY.mkdir(mode=0o700, parents=True, exist_ok=True)
    path = CACHE_DIRECTORY / 'authentication.key'
    try:
        with path.open('xb') as stream:
            stream.write(os.urandom(32))
        if os.name != 'nt':
            path.chmod(0o600)
    except FileExistsError:
        pass
    return path.read_bytes()


def run(options: Serve | Upload | Bootloader | Provision) -> None:
    try:
        if isinstance(options, Provision):
            updater_provision.provision(CACHE_DIRECTORY / 'tytools-0.9.9')
        elif isinstance(options, Serve):
            serve(options.serial)
        elif isinstance(options, Bootloader):
            request_bootloader(options.serial)
        else:
            result = request_upload(options)
            print(result.output, end='')
            sys.exit(result.returncode)
    except KeyboardInterrupt:
        sys.exit(130)
    except (
        OSError,
        EOFError,
        AuthenticationError,
        ValueError,
        subprocess.CalledProcessError,
    ) as error:
        sys.exit(f'Upload helper: {error}')


def main() -> None:
    run(tyro.cli(Serve | Upload | Bootloader | Provision))


REQUEST_LIMIT = 8192
CACHE_DIRECTORY = cache_directory() / 'cntrl-upload'
IPC_FAMILY = 'AF_PIPE' if os.name == 'nt' else 'AF_UNIX'
SOCKET_PATH = (
    r'\\.\pipe\cntrl-upload' if os.name == 'nt' else CACHE_DIRECTORY / 'helper.sock'
)
FLASH_CONFIG_SHA256 = '65b509525e8b0e6e92e4e6bc8fa39575399b27532e04a55f7a7883212042fe7a'
