"""Isolated splash preparation/upload commands; no firmware flashing."""

import sys
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel

from ..patch_switcher.compiler import read_document
from ..patch_switcher.models import Target
from ..patch_switcher.transport import SerialConnection, SetupClient
from .asset import convert_legacy, inspect_asset, prepare_movie


class Prepare(BaseModel, frozen=True):
    source: Annotated[Path, tyro.conf.Positional]
    output: Path
    target: Path


class Convert(BaseModel, frozen=True):
    """Convert a legacy asset without changing its precomputed pixel streams."""

    source: Annotated[Path, tyro.conf.Positional]
    output: Path
    target: Path


class Upload(BaseModel, frozen=True):
    asset: Annotated[Path, tyro.conf.Positional]
    port: Path


def upload_asset(port: Path, data: bytes) -> str:
    """Use an independent, chunk-acknowledged protocol for SD-sized assets."""
    inspect_asset(data)
    with SerialConnection(port) as connection:
        return SetupClient(connection).copy_movie(data)


def main() -> None:
    options = tyro.cli(Prepare | Convert | Upload)
    try:
        if isinstance(options, (Prepare, Convert)):
            target = Target.model_validate(read_document(options.target))
            geometry = target.animation
            if geometry is None:
                raise ValueError('target must declare animation geometry')
            data = (
                prepare_movie(options.source, geometry)
                if isinstance(options, Prepare)
                else convert_legacy(options.source.read_bytes(), geometry)
            )
            info = inspect_asset(data)
            options.output.parent.mkdir(parents=True, exist_ok=True)
            options.output.write_bytes(data)
            print(
                f'{options.output}: {info.frames} frames, '
                f'{info.duration_ms} ms, {info.size} bytes'
            )
        else:
            print(upload_asset(options.port, options.asset.read_bytes()))
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError) as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
