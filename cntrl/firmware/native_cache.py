"""Content-addressed executables shared across native build directories."""

import os
import subprocess
import sys
from hashlib import sha256
from pathlib import Path
from shutil import copy2, which
from tempfile import TemporaryDirectory


def build_key(command: list[str], directory: Path, inputs: list[Path]) -> str:
    compiler = which(command[0])
    if compiler is None:
        raise FileNotFoundError(f'native compiler not found: {command[0]}')
    compiler_path = Path(compiler).resolve()
    # Temporary output/include paths differ between pytest runs. Their contents
    # below, rather than their locations, determine whether a build is reusable.
    signature = sha256(
        repr([a.replace(str(directory), '<build>') for a in command]).encode()
    )
    signature.update(str(compiler_path).encode())
    if sys.platform == 'win32':
        signature.update(sha256(compiler_path.read_bytes()).digest())
    else:
        version = subprocess.run(
            [str(compiler_path), '--version'], check=True, capture_output=True
        )
        signature.update(version.stdout)
        signature.update(version.stderr)
    for name in BUILD_ENVIRONMENT:
        signature.update(repr((name, os.environ.get(name))).encode())
    for path in sorted(
        set(inputs), key=lambda p: str(p).replace(str(directory), '<build>')
    ):
        signature.update(str(path).replace(str(directory), '<build>').encode() + b'\0')
        signature.update(sha256(path.read_bytes()).digest())
    return signature.hexdigest()


def restore(cache: Path, key: str, output: Path) -> bool:
    artifact = cache / key
    if not artifact.is_file():
        return False
    copy_artifact(artifact, output)
    return True


def store(cache: Path, key: str, output: Path) -> None:
    cache.mkdir(parents=True, exist_ok=True)
    copy_artifact(output, cache / key)


def copy_artifact(source: Path, destination: Path) -> None:
    # Publish only complete binaries, including when separate pytest processes
    # compile the same key concurrently. Never hardlink mutable build outputs.
    with TemporaryDirectory(dir=destination.parent, prefix='native-copy-') as name:
        temporary = Path(name) / 'executable'
        copy2(source, temporary)
        temporary.replace(destination)


BUILD_ENVIRONMENT = (
    'CPATH',
    'CPLUS_INCLUDE_PATH',
    'LIBRARY_PATH',
    'SDKROOT',
    'MACOSX_DEPLOYMENT_TARGET',
    'CL',
    '_CL_',
    'INCLUDE',
    'LIB',
    'LIBPATH',
)
