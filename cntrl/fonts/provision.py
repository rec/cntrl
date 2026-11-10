"""Provision the pinned native rasterizer without changing Python binding versions."""

import hashlib
import os
import platform
import subprocess
import tarfile
from importlib.metadata import distribution
from pathlib import Path
from urllib.request import urlopen

import tomlkit
import tyro
from pydantic import BaseModel


class Provision(BaseModel, frozen=True):
    build_directory: Path = Path('build/freetype')


def provision(options: Provision) -> None:
    root = (
        options.build_directory.resolve() / f'{platform.system()}-{platform.machine()}'
    )
    root.mkdir(parents=True, exist_ok=True)
    prefix = root / 'install'
    name = (
        'libfreetype.dll'
        if os.name == 'nt'
        else 'libfreetype.dylib'
        if platform.system() == 'Darwin'
        else 'libfreetype.so'
    )
    library = prefix / ('bin/freetype.dll' if os.name == 'nt' else f'lib/{name}')
    patch = Path(__file__).with_name('freetype-mono64.patch')
    identity = hashlib.sha256(
        Path(__file__).read_bytes() + patch.read_bytes()
    ).hexdigest()
    marker = root / 'provenance.toml'
    if (
        not marker.exists()
        or tomlkit.parse(marker.read_text())['recipe'] != identity
        or not library.exists()
    ):
        sources: dict[str, Path] = {}
        for package, (url, digest) in SOURCES.items():
            archive = root / (package + '.tar.xz')
            if not archive.exists():
                with urlopen(url, timeout=60) as response:
                    archive.write_bytes(response.read())
            if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
                raise ValueError(f'{package}: source archive checksum mismatch')
            with tarfile.open(archive) as source:
                source.extractall(root, filter='data')
            sources[package] = root / package
        # Keep monochrome coordinates and the profile pool layout identical
        # across LP64 (macOS/Linux) and LLP64 (Windows), without changing the ABI.
        patch_command = [
            'git',
            'apply',
            '--unidiff-zero',
            '--unsafe-paths',
            f'--directory={sources["freetype-2.14.3"].as_posix()}',
        ]
        subprocess.run([*patch_command, '--check', str(patch)], check=True, timeout=30)
        subprocess.run([*patch_command, str(patch)], check=True, timeout=30)
        common = [
            f'-DCMAKE_INSTALL_PREFIX={prefix}',
            f'-DCMAKE_PREFIX_PATH={prefix}',
            '-DCMAKE_BUILD_TYPE=Release',
            '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
            '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        ]
        freetype = [
            '-DFT_DISABLE_PNG=TRUE',
            '-DFT_DISABLE_BZIP2=TRUE',
            '-DFT_DISABLE_ZLIB=TRUE',
            '-DFT_DISABLE_BROTLI=TRUE',
        ]
        cmake(
            sources['freetype-2.14.3'],
            root / 'ft-static',
            [
                *common,
                *freetype,
                '-DBUILD_SHARED_LIBS=OFF',
                '-DFT_DISABLE_HARFBUZZ=TRUE',
            ],
        )
        cmake(
            sources['harfbuzz-8.3.0'],
            root / 'hb-static',
            [
                *common,
                '-DBUILD_SHARED_LIBS=OFF',
                '-DHB_HAVE_FREETYPE=ON',
                '-DHB_HAVE_GLIB=OFF',
                '-DHB_HAVE_ICU=OFF',
                '-DHB_HAVE_CORETEXT=OFF',
                '-DHB_BUILD_UTILS=OFF',
                '-DHB_BUILD_SUBSET=OFF',
            ],
        )
        harfbuzz = prefix / (
            'lib/harfbuzz.lib' if os.name == 'nt' else 'lib/libharfbuzz.a'
        )
        cmake(
            sources['freetype-2.14.3'],
            root / 'ft-shared',
            [
                *common,
                *freetype,
                '-DBUILD_SHARED_LIBS=ON',
                '-DFT_REQUIRE_HARFBUZZ=TRUE',
                f'-DHarfBuzz_INCLUDE_DIR={prefix / "include/harfbuzz"}',
                f'-DHarfBuzz_LIBRARY={harfbuzz}',
            ],
        )
        marker.write_text(
            tomlkit.dumps(
                {
                    'recipe': identity,
                    'freetype': '2.14.3',
                    'harfbuzz': '8.3.0',
                    'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
                }
            )
        )
    package = Path(str(distribution('freetype-py').locate_file('freetype')))
    destination = package / name
    data = library.read_bytes()
    # Windows cannot replace a DLL loaded by another qualification process.
    if not destination.exists() or destination.read_bytes() != data:
        # Replace the entry so uv's cached wheel hard links stay untouched.
        temporary = destination.with_suffix(destination.suffix + '.new')
        temporary.write_bytes(data)
        temporary.replace(destination)
    print(f'Provisioned FreeType 2.14.3: {destination}')


def cmake(source: Path, build: Path, options: list[str]) -> None:
    subprocess.run(
        ['cmake', '-S', str(source), '-B', str(build), *options],
        check=True,
        timeout=180,
    )
    subprocess.run(
        [
            'cmake',
            '--build',
            str(build),
            '--config',
            'Release',
            '--parallel',
            '2',
            '--target',
            'install',
        ],
        check=True,
        timeout=900,
    )


SOURCES = {
    'freetype-2.14.3': (
        'https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz',
        '36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f',
    ),
    'harfbuzz-8.3.0': (
        'https://github.com/harfbuzz/harfbuzz/releases/download/8.3.0/harfbuzz-8.3.0.tar.xz',
        '109501eaeb8bde3eadb25fab4164e993fbace29c3d775bcaa1c1e58e2f15f847',
    ),
}


if __name__ == '__main__':
    provision(tyro.cli(Provision))
