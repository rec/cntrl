"""Isolated splash preparation/upload commands; no firmware flashing."""

import subprocess
import sys
from pathlib import Path
from typing import Annotated
from zlib import crc32

import tyro
from pydantic import BaseModel

from .....patch_switcher.transport import SerialConnection
from .asset import inspect_asset, prepare_gif


class Prepare(BaseModel, frozen=True):
    source: Annotated[Path, tyro.conf.Positional]
    output: Path


class Upload(BaseModel, frozen=True):
    asset: Annotated[Path, tyro.conf.Positional]
    port: Path


def upload_asset(port: Path, data: bytes) -> str:
    """Use an independent, chunk-acknowledged protocol for SD-sized assets."""
    inspect_asset(data)
    checksum = crc32(data)
    with SerialConnection(port) as connection:
        connection.write(f'CNTRL SPLASH BEGIN {len(data)} {checksum:08x}\n'.encode())
        if (response := connection.line()) != 'CNTRL SPLASH SEND':
            raise ValueError(response)
        for offset in range(0, len(data), 512):
            chunk = data[offset : offset + 512]
            connection.write(chunk)
            received = offset + len(chunk)
            expected = (
                f'CNTRL SPLASH NEXT {received}'
                if received < len(data)
                else f'CNTRL SPLASH DONE {len(data)} {checksum:08x}'
            )
            if (response := connection.line()) != expected:
                raise ValueError(response)
        connection.write(b'CNTRL SPLASH STATUS\n')
        if (response := connection.line()) != (
            f'CNTRL SPLASH READY {len(data)} {checksum:08x}'
        ):
            raise ValueError(f'splash upload verification failed: {response}')
    return response


def main() -> None:
    options = tyro.cli(Prepare | Upload)
    try:
        if isinstance(options, Prepare):
            data = prepare_gif(options.source)
            info = inspect_asset(data)
            options.output.parent.mkdir(parents=True, exist_ok=True)
            options.output.write_bytes(data)
            print(
                f'{options.output}: {info.frames} frames, '
                f'{info.duration_ms} ms, {info.size} bytes'
            )
        else:
            print(upload_asset(options.port, options.asset.read_bytes()))
    except subprocess.CalledProcessError as error:
        sys.exit(error.stderr.decode().strip())
    except (OSError, ValueError) as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
