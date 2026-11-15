"""Verify wheel/sdist resource integrity and absence of vendor firmware."""

import hashlib
import tarfile
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

import tomlkit
import tyro
from pydantic import BaseModel

from ....firmware import distribution
from ....firmware.distribution import Manifest
from . import release
from .boot_switch import STOCK_SHA256


class Check(BaseModel, frozen=True):
    artifacts: list[Path]
    installation: Path | None = None


def check(artifact: Path) -> None:
    if artifact.suffix == '.whl':
        with ZipFile(artifact) as archive:
            contents = {
                n: archive.read(n) for n in archive.namelist() if not n.endswith('/')
            }
    else:
        with tarfile.open(artifact) as archive:
            contents: dict[str, bytes] = {}
            for member in archive.getmembers():
                if member.isfile():
                    stream = archive.extractfile(member)
                    assert stream is not None
                    contents[member.name] = stream.read()
    manifests = [
        n
        for n in contents
        if n.endswith('/cntrl/maker/morningstar/mc/distribution/manifest.toml')
        or n == 'cntrl/maker/morningstar/mc/distribution/manifest.toml'
    ]
    if len(manifests) != 1:
        raise ValueError(f'{artifact}: expected one packaged resource manifest')
    path = manifests[0]
    manifest = Manifest.model_validate(tomlkit.parse(contents[path].decode()).unwrap())
    prefix = path.removesuffix('manifest.toml')
    for name, digest in manifest.files.items():
        if hashlib.sha256(contents[prefix + name]).hexdigest() != digest:
            raise ValueError(f'{artifact}: resource differs: {name}')
    for name, data in contents.items():
        if name.endswith('.hex') or '/.tools/' in name or '/firmware/build/' in name:
            raise ValueError(f'{artifact}: development/vendor image included: {name}')
        if hashlib.sha256(data).hexdigest() == STOCK_SHA256:
            raise ValueError(f'{artifact}: vendor stock firmware included: {name}')
    print(
        f'{artifact}: own-only resource set verified, source {manifest.source_commit}'
    )


def main() -> None:
    options = tyro.cli(Check)
    for artifact in options.artifacts:
        check(artifact)
    if options.installation is not None:
        resources = Path(__file__).with_name('distribution')
        first = options.installation / 'first'
        second = options.installation / 'offline'
        release.prepare(resources, first)
        with patch.object(
            distribution,
            'urlopen',
            side_effect=AssertionError('warm cache attempted a download'),
        ):
            release.prepare(resources, second)
        if (first / 'firmware.hex').read_bytes() != (
            second / 'firmware.hex'
        ).read_bytes():
            raise ValueError('offline composition differs from the initial preparation')
        print('Installed resource preparation and offline cache reuse verified')


if __name__ == '__main__':
    main()
