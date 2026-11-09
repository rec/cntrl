"""A manually launched, single-pedal upload service over authenticated local IPC."""

import hashlib
import os
import struct
import subprocess
import sys
from multiprocessing.connection import Client, Listener
from multiprocessing.context import AuthenticationError
from pathlib import Path
from tempfile import TemporaryDirectory

import tyro
from pydantic import BaseModel, ConfigDict, Field, TypeAdapter
from serial import Serial

from ....firmware.intel_hex import FirmwareImage, read_hex
from ....patch_switcher.devices import connected


class Serve(BaseModel, frozen=True):
    """Serve upload requests. Launch manually from Terminal, not from Codex."""


class Upload(BaseModel, frozen=True):
    """Upload a registered worktree's HEX image only if its SHA-256 matches."""

    image: Path
    sha256: str = Field(pattern=r'^[0-9a-f]{64}$')

    model_config = ConfigDict(extra='forbid')


class Bootloader(BaseModel, frozen=True):
    """Enter HalfKay through the fixed pedal's USB serial port, without uploading."""


class UploadResult(BaseModel, frozen=True):
    returncode: int
    output: str


class Board(BaseModel, frozen=True):
    tag: str
    serial: str
    description: str
    model: str
    capabilities: list[str]


def project_worktrees() -> list[Path]:
    """Read Git's registered checkouts, with the primary checkout first."""
    result = subprocess.run(
        ['git', '-C', str(ROOT), 'worktree', 'list', '--porcelain', '-z'],
        capture_output=True,
        check=True,
    )
    return [
        Path(f.removeprefix('worktree ')).resolve()
        for f in result.stdout.decode().split('\0')
        if f.startswith('worktree ')
    ]


def request_bootloader() -> None:
    """Select the Teensy USB core's special 134-baud bootloader request."""
    ports = [d.port for d in connected() if d.serial == BOARD_SERIAL]
    if len(ports) != 1:
        raise ValueError('the expected pedal is not uniquely present in serial mode')
    with Serial(str(ports[0]), baudrate=134, timeout=5, write_timeout=5):
        pass
    # Do not restore line coding: USB must receive the reboot request.
    print('Bootloader requested; verify HalfKay enumeration before uploading.')


def validate_firmware(image: FirmwareImage) -> None:
    """Check the target's boot-image envelope, not the safety of its instructions."""
    if (
        image.origin != 0x60000000
        or image.entry != 0x60001000
        or len(image.regions) != 1
        or not 0x2000 <= len(image.data) <= 0x7C0000
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
        or boot != 0x60001020
        or self_address != 0x60001000
        or not reset & 1
        or not 0x60001400 <= reset - 1 < csf
        or csf != image.origin + len(image.data) - 3072
        or image.data[-3072:] != bytes(3072)
        or struct.unpack_from('<3I', image.data, 0x1020)
        != (image.origin, len(image.data), 0)
    ):
        raise ValueError('unexpected boot header, reset entry, boot data or CSF')


def updater_path() -> Path:
    """Use the explicit updater, or the installed macOS application."""
    if executable := os.environ.get('CNTRL_TYCMD'):
        return Path(executable)
    if sys.platform != 'darwin':
        raise ValueError(
            f'no default updater for {sys.platform}; set CNTRL_TYCMD explicitly'
        )
    return (
        Path('/Applications/Morningstar Firmware Updater.app')
        / 'Contents/Resources/extraResources/tycmd'
    )


def upload_image(request: Upload) -> UploadResult:
    """Snapshot a worktree image, validate it, then upload to the sole target."""
    if not request.image.is_absolute():
        raise ValueError('firmware image path must be absolute')
    source = request.image.resolve(strict=True)
    if source.suffix != '.hex' or not any(
        source.is_relative_to(w) for w in project_worktrees()
    ):
        raise ValueError('expected a HEX file inside a registered project worktree')
    CACHE_DIRECTORY.mkdir(mode=0o700, parents=True, exist_ok=True)
    with TemporaryDirectory(prefix='upload-', dir=CACHE_DIRECTORY) as directory:
        snapshot = Path(directory) / 'patch-switcher.hex'
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
            if b.tag == BOARD_TAG
            and b.serial == BOARD_SERIAL
            and b.description == 'HalfKay'
            and b.model == 'Teensy 4.1'
            and 'upload' in b.capabilities
        ]
        if len(matches) != 1:
            raise ValueError(
                'the expected pedal is not uniquely present in HalfKay mode'
            )
        print(f'Uploading SHA-256 {image.sha256} to {BOARD_TAG}', flush=True)
        result = subprocess.run(
            [updater, 'upload', '--board', BOARD_TAG, str(snapshot)],
            capture_output=True,
            text=True,
            check=False,
        )
        output = result.stdout + result.stderr
        print(output, end='', flush=True)
        return UploadResult(returncode=result.returncode, output=output)


def process_request(line: bytes) -> UploadResult:
    """Accept one bounded JSON object containing an image path and digest."""
    try:
        if len(line) > REQUEST_LIMIT or not line.endswith(b'\n'):
            raise ValueError(
                f'expected a newline-terminated request (maximum {REQUEST_LIMIT} bytes)'
            )
        return upload_image(Upload.model_validate_json(line))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        return UploadResult(returncode=1, output=f'Upload helper: {error}\n')


def serve() -> None:
    """Handle one upload at a time; never replace an existing socket."""
    updater_path()
    if os.name != 'nt':
        os.umask(0o077)
    with Listener(
        str(SOCKET_PATH), family=IPC_FAMILY, authkey=authentication_key()
    ) as listener:
        try:
            print(f'Upload-only helper listening on {SOCKET_PATH}', flush=True)
            print('Enter HalfKay before requesting upload. Ctrl-C stops the helper.')
            while True:
                try:
                    with listener.accept() as connection:
                        if connection.poll(5):
                            result = process_request(
                                connection.recv_bytes(REQUEST_LIMIT)
                            )
                            connection.send_bytes(result.model_dump_json().encode())
                except (OSError, EOFError, AuthenticationError) as error:
                    # A disconnected client must not cancel an upload in progress.
                    print(f'Upload client disconnected: {error}', file=sys.stderr)
        finally:
            print('Upload helper stopped.', flush=True)


def request_upload(request: Upload) -> UploadResult:
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


def run(options: Serve | Upload | Bootloader) -> None:
    try:
        if isinstance(options, Serve):
            serve()
        elif isinstance(options, Bootloader):
            request_bootloader()
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
    run(tyro.cli(Serve | Upload | Bootloader))


ROOT = Path(__file__).resolve().parent
REQUEST_LIMIT = 8192
CACHE_DIRECTORY = (
    Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local'))
    if os.name == 'nt'
    else Path.home() / 'Library/Caches'
    if sys.platform == 'darwin'
    else Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache'))
) / 'cntrl-upload'
IPC_FAMILY = 'AF_PIPE' if os.name == 'nt' else 'AF_UNIX'
SOCKET_PATH = (
    r'\\.\pipe\cntrl-upload-14468280'
    if os.name == 'nt'
    else CACHE_DIRECTORY / 'helper.sock'
)
BOARD_SERIAL = '14468280'
BOARD_TAG = '14468280-Teensy'
FLASH_CONFIG_SHA256 = '65b509525e8b0e6e92e4e6bc8fa39575399b27532e04a55f7a7883212042fe7a'


if __name__ == '__main__':
    main()
