"""Install the pinned TyTools uploader without accessing a USB device."""

import hashlib
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from urllib.request import urlopen
from zipfile import ZipFile


def installed(directory: Path) -> Path:
    if sys.platform not in BINARIES:
        raise ValueError(
            'TyTools 0.9.9 has no pinned archive for this host; '
            'set CNTRL_TYCMD explicitly'
        )
    path = directory / ('tycmd.exe' if sys.platform == 'win32' else 'tycmd')
    if not path.is_file():
        raise ValueError(
            'run cntrl upload-helper --target-dir cntrl/maker/morningstar/mc '
            'provision before starting the helper'
        )
    if hashlib.sha256(path.read_bytes()).hexdigest() != BINARIES[sys.platform]:
        raise ValueError('pinned TyTools executable checksum mismatch')
    return path


def provision(directory: Path) -> Path:
    if sys.platform not in ARCHIVES:
        raise ValueError(
            'TyTools 0.9.9 has no pinned archive for this host; '
            'set CNTRL_TYCMD explicitly'
        )
    name, digest = ARCHIVES[sys.platform]
    destination = directory / ('tycmd.exe' if sys.platform == 'win32' else 'tycmd')
    if (
        destination.is_file()
        and hashlib.sha256(destination.read_bytes()).hexdigest()
        == BINARIES[sys.platform]
    ):
        return installed(directory)
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    archive = directory / name
    if not archive.is_file():
        with urlopen(
            f'https://github.com/Koromix/tytools/releases/download/v{VERSION}/{name}',
            timeout=60,
        ) as response:
            content = response.read()
        if hashlib.sha256(content).hexdigest() != digest:
            raise ValueError('TyTools archive checksum mismatch')
        archive.write_bytes(content)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise ValueError('TyTools archive checksum mismatch')
    if sys.platform == 'win32':
        with ZipFile(archive) as package:
            content = package.read('tycmd.exe')
    else:
        with TemporaryDirectory(prefix='mount-', dir=directory) as temporary:
            mount = Path(temporary) / 'volume'
            mount.mkdir()
            subprocess.run(
                [
                    'hdiutil',
                    'attach',
                    '-readonly',
                    '-nobrowse',
                    '-mountpoint',
                    str(mount),
                    str(archive),
                ],
                check=True,
                capture_output=True,
            )
            try:
                candidates = [p for p in mount.rglob('tycmd') if p.is_file()]
                if len(candidates) != 1:
                    raise ValueError(
                        'expected one tycmd in the pinned image: '
                        f'{[str(p.relative_to(mount)) for p in candidates]}'
                    )
                content = candidates[0].read_bytes()
            finally:
                subprocess.run(
                    ['hdiutil', 'detach', str(mount)], check=True, capture_output=True
                )
    if hashlib.sha256(content).hexdigest() != BINARIES[sys.platform]:
        raise ValueError('TyTools executable checksum mismatch')
    staged = directory / 'tycmd.staging'
    staged.write_bytes(content)
    staged.chmod(0o755)
    staged.replace(destination)
    print(
        f'TyTools {VERSION}: {destination}\n'
        f'SHA-256 {hashlib.sha256(content).hexdigest()}',
        flush=True,
    )
    return installed(directory)


VERSION = '0.9.9'
BINARIES = {
    'darwin': '2118d04e3bef846b6d7a6fca26e08a177e97f0458708da4a4013309d013b7246',
    'win32': 'cd4d199bd33bce2143ce76bfb721587f3bd0b17a3375c613e86834435fc3a5d0',
}
ARCHIVES = {
    'darwin': (
        'tytools_0.9.9_osx.dmg',
        '4f4830a574ffa23622e15823734743ec79cc603123bce64c9d4b07714f6d94d6',
    ),
    'win32': (
        'tytools_0.9.9_win64.zip',
        '1ff7c945de4789950fb6f2d56bac8523e849b86a44c47b87e2c77d130af559b2',
    ),
}
