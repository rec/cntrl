"""A manually launched, single-pedal firmware upload service over a Unix socket."""

import hashlib
import os
import socket
import struct
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import tyro
from pydantic import BaseModel, ConfigDict, Field, TypeAdapter

from .firmware import FirmwareImage, read_hex


class Serve(BaseModel, frozen=True):
    """Serve upload requests. Launch manually from Terminal, not from Codex."""


class Upload(BaseModel, frozen=True):
    """Upload the fixed project image only if its SHA-256 matches."""

    sha256: str = Field(pattern=r'^[0-9a-f]{64}$')

    model_config = ConfigDict(extra='forbid')


class UploadResult(BaseModel, frozen=True):
    returncode: int
    output: str


class Board(BaseModel, frozen=True):
    tag: str
    serial: str
    description: str
    model: str
    capabilities: list[str]


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


def upload_image(request: Upload) -> UploadResult:
    """Snapshot and validate the sole allowed image, then upload to the sole target."""
    CACHE_DIRECTORY.mkdir(mode=0o700, parents=True, exist_ok=True)
    with TemporaryDirectory(prefix='upload-', dir=CACHE_DIRECTORY) as directory:
        snapshot = Path(directory) / 'usb_probe.hex'
        snapshot.write_bytes(IMAGE_PATH.read_bytes())
        image = read_hex(snapshot)
        if image.sha256 != request.sha256:
            raise ValueError('firmware SHA-256 changed; no upload was attempted')
        validate_firmware(image)
        listing = subprocess.run(
            [str(UPLOADER_PATH), 'list', '--verbose', '--output', 'json'],
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
            [str(UPLOADER_PATH), 'upload', '--board', BOARD_TAG, str(snapshot)],
            capture_output=True,
            text=True,
            check=False,
        )
        output = result.stdout + result.stderr
        print(output, end='', flush=True)
        return UploadResult(returncode=result.returncode, output=output)


def process_request(line: bytes) -> UploadResult:
    """The protocol accepts only one bounded JSON object containing a digest."""
    try:
        if len(line) > 256 or not line.endswith(b'\n'):
            raise ValueError(
                'expected a newline-terminated request of at most 256 bytes'
            )
        return upload_image(Upload.model_validate_json(line))
    except (OSError, ValueError) as error:
        return UploadResult(returncode=1, output=f'Upload helper: {error}\n')


def serve() -> None:
    """Handle one upload at a time; never replace an existing socket."""
    os.umask(0o077)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
        listener.bind(str(SOCKET_PATH))
        try:
            listener.listen(1)
            print(f'Upload-only helper listening on {SOCKET_PATH}', flush=True)
            print('Press PROGRAM before requesting upload. Ctrl-C stops the helper.')
            while True:
                connection, _ = listener.accept()
                with connection:
                    connection.settimeout(5)
                    try:
                        with connection.makefile('rwb') as stream:
                            result = process_request(stream.readline(257))
                            stream.write(result.model_dump_json().encode() + b'\n')
                            stream.flush()
                    except OSError as error:
                        # A disconnected client must not cancel an upload in progress.
                        print(f'Upload client disconnected: {error}', file=sys.stderr)
        finally:
            SOCKET_PATH.unlink()


def request_upload(request: Upload) -> UploadResult:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.connect(str(SOCKET_PATH))
        with connection.makefile('rwb') as stream:
            stream.write(request.model_dump_json().encode() + b'\n')
            stream.flush()
            return UploadResult.model_validate_json(stream.readline())


def main() -> None:
    options = tyro.cli(Serve | Upload)
    try:
        if isinstance(options, Serve):
            serve()
        else:
            result = request_upload(options)
            print(result.output, end='')
            sys.exit(result.returncode)
    except KeyboardInterrupt:
        sys.exit(130)
    except OSError as error:
        sys.exit(f'Upload helper: {error}')


ROOT = Path(__file__).resolve().parent
IMAGE_PATH = ROOT / 'firmware/build/usb_probe.ino.hex'
SOCKET_PATH = ROOT / 'firmware/.upload-helper.sock'
CACHE_DIRECTORY = Path.home() / 'Library/Caches/cntrl-upload'
UPLOADER_PATH = (
    Path('/Applications/Morningstar Firmware Updater.app')
    / 'Contents/Resources/extraResources/tycmd'
)
BOARD_SERIAL = '14468280'
BOARD_TAG = '14468280-Teensy'
FLASH_CONFIG_SHA256 = '65b509525e8b0e6e92e4e6bc8fa39575399b27532e04a55f7a7883212042fe7a'


if __name__ == '__main__':
    main()
