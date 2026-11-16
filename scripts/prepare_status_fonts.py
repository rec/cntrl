"""Reproduce the packaged bitmap comparison fonts from pinned upstream sources.

Run with: uv run --no-sync python -m scripts.prepare_status_fonts
"""

import subprocess
import tarfile
from hashlib import sha256
from io import BytesIO
from pathlib import Path
from tempfile import TemporaryDirectory
from urllib.request import urlopen

import tomlkit
import tyro
from pydantic import BaseModel

from cntrl.fonts.bdf import prepare


class PrepareStatusFonts(BaseModel, frozen=True):
    output: Path = Path('cntrl/fonts/bitmap_samples')


class FontFile(BaseModel, frozen=True):
    file: str
    label: str
    size: int


class Source(BaseModel, frozen=True):
    repo: str
    rev: str
    files: list[FontFile]


def download(url: str) -> bytes:
    with urlopen(url, timeout=30) as response:
        return response.read()


def main() -> None:
    options = tyro.cli(PrepareStatusFonts)
    options.output.mkdir(parents=True, exist_ok=True)
    archive = download(UW_URL)
    if sha256(archive).hexdigest() != UW_SHA256:
        raise ValueError('UW ttyp0 archive checksum mismatch')
    samples: list[dict[str, str | int]] = []
    with TemporaryDirectory(prefix='cntrl-font-sources-') as folder:
        with tarfile.open(fileobj=BytesIO(archive), mode='r:gz') as release:
            release.extractall(folder, filter='data')
        source = Path(folder) / 'uw-ttyp0-2.1'
        targets = [
            f'genbdf/t0-{s}{b}-uni.bdf'
            for s in [11, 12, 13, 14, 15, 16]
            for b in ['', 'b']
        ]
        subprocess.run(
            ['make', '-f', 'Makefile.in', *targets],
            cwd=source,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        (options.output / 'UW-ttyp0-LICENSE.txt').write_bytes(
            (source / 'LICENSE').read_bytes()
        )
        for size, width in [(12, 6), (11, 6), (13, 7), (14, 7), (15, 8), (16, 8)]:
            for suffix, weight in [('', 'R'), ('b', 'B')]:
                filename = f'UW-ttyp0-{size}{suffix}.cfont'
                (options.output / filename).write_bytes(
                    prepare(
                        (source / f'genbdf/t0-{size}{suffix}-uni.bdf').read_bytes(),
                        size,
                    )
                )
                samples.append(
                    dict(
                        name=f'UW ttyp0 {weight} {width}x{size}',
                        file=filename,
                        size=size,
                    )
                )
    for family, details in SOURCES.items():
        base = f'https://raw.githubusercontent.com/{details.repo}/{details.rev}'
        (options.output / f'{family}-LICENSE.txt').write_bytes(
            download(f'{base}/LICENSE')
        )
        for entry in details.files:
            data = download(f'{base}/{entry.file}')
            filename = f'{family}-{Path(entry.file).stem}.cfont'
            (options.output / filename).write_bytes(prepare(data, entry.size))
            samples.append(
                dict(name=f'{family} {entry.label}', file=filename, size=entry.size)
            )
    (options.output / 'comparison.toml').write_text(tomlkit.dumps({'samples': samples}))


UW_URL = 'https://people.mpi-inf.mpg.de/~uwe/misc/uw-ttyp0/uw-ttyp0-2.1.tar.gz'
UW_SHA256 = '99506dd8794e1a5d5cd5811e6c1e55eeef989f8c3501fdb9265bda972aba1630'


SOURCES = {
    'Cherry': Source(
        repo='turquoise-hexagon/cherry',
        rev='34a94dc3db4ad1136bbcce9f8dff154ef5fa901a',
        files=[
            FontFile(file=f'cherry-{s}-{w}.bdf', label=f'{w.upper()} {s}px', size=s)
            for s in [10, 11, 12, 13]
            for w in ['r', 'b']
        ],
    ),
    'Tamzen': Source(
        repo='sunaku/tamzen-font',
        rev='3255e8259bc9b880c60ab8b737ec8aa574e00d75',
        files=[
            FontFile(
                file=f'bdf/Tamzen{w}x{s}{b}.bdf', label=f'{b.upper()} {w}x{s}', size=s
            )
            for w, s in [(5, 9), (6, 12), (7, 13), (7, 14), (8, 15), (8, 16)]
            for b in ['r', 'b']
        ],
    ),
    'Spleen': Source(
        repo='fcambus/spleen',
        rev='c73273a07deb838a99ac473dee5f340ffa901d7d',
        files=[
            FontFile(file=f'spleen-{w}x{s}.bdf', label=f'{w}x{s}', size=s)
            for w, s in [(5, 8), (6, 12), (8, 16)]
        ],
    ),
}


if __name__ == '__main__':
    main()
