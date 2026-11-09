"""Prepare configured movies and pack their SD asset references into setups."""

import struct
from pathlib import Path
from zlib import crc32

from ..patch_switcher.models import Setup
from .asset import Geometry, inspect_asset, prepare_movie


def prepare(
    setup: Setup, assets: dict[int, bytes], geometry: Geometry | None
) -> tuple[Setup, bytes]:
    if (setup.splash_movie or setup.idle_movie) and geometry is None:
        raise ValueError('target must declare animation geometry')
    keys: list[int] = []
    for n in setup.splash_movie + setup.idle_movie:
        assert geometry is not None
        path = Path(n)
        if not path.is_file():
            raise ValueError(f'{path}: movie file does not exist')
        data = (
            path.read_bytes()
            if path.suffix.lower() == '.cnsa'
            else prepare_movie(path, geometry)
        )
        info = inspect_asset(data)
        if (info.width, info.height, info.streams) != (
            geometry.width,
            geometry.height,
            len(geometry.streams),
        ):
            raise ValueError(f'{path}: animation geometry differs from target')
        key = crc32(data)
        if key in assets and assets[key] != data:
            raise ValueError(f'{path}: movie checksum collision')
        assets[key] = data
        keys.append(key)
    records = struct.pack(
        '<4sHH', b'CMOV', len(setup.splash_movie), len(setup.idle_movie)
    ) + b''.join(struct.pack('<I', k) for k in keys)
    paths = [f'movies/{k:08x}.cnsa' for k in keys]
    return setup.model_copy(
        update={
            'splash_movie': paths[: len(setup.splash_movie)],
            'idle_movie': paths[len(setup.splash_movie) :],
        }
    ), records
