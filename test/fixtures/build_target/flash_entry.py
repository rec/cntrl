"""Record the portable flash contract without touching hardware."""

from pathlib import Path

import tyro
from pydantic import BaseModel


class Flash(BaseModel, frozen=True):
    directory: Path
    port: Path
    legacy_usb: bool = False


if __name__ == '__main__':
    command = tyro.cli(Flash)
    (command.directory / 'flashed.txt').write_bytes(
        f'{command.port}\n{command.legacy_usb}\n'.encode()
    )
