"""Content-addressed executables shared across native build directories."""

import os
import re
import subprocess
import sys
from hashlib import sha256
from pathlib import Path
from shutil import copy2, which
from stat import S_ISREG
from tempfile import TemporaryDirectory

from ..file_lock import WriterLockHeld, writer_lock

MAX_CACHE_BYTES = 2 * 1024**3


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
    if not cache.is_dir():
        return False
    try:
        with writer_lock(cache, operation='native cache operation'):
            artifact = cache / key
            if not artifact.is_file(follow_symlinks=False):
                return False
            copy_artifact(artifact, output)
            os.utime(artifact, None)
    except WriterLockHeld:
        return False
    return True


def store(cache: Path, key: str, output: Path) -> None:
    size = output.stat().st_size
    if size > MAX_CACHE_BYTES:
        return
    try:
        with writer_lock(cache, operation='native cache operation'):
            make_room(cache, key, size)
            artifact = cache / key
            copy_artifact(output, artifact)
            os.utime(artifact, None)
    except WriterLockHeld:
        return


def make_room(cache: Path, key: str, size: int) -> None:
    entries: list[tuple[int, Path, int]] = []
    for path in cache.iterdir():
        if path.name != key and re.fullmatch(r'[0-9a-f]{64}', path.name):
            info = path.lstat()
            if S_ISREG(info.st_mode):
                entries.append((info.st_mtime_ns, path, info.st_size))
    total = size + sum(n for _, _, n in entries)
    if total > MAX_CACHE_BYTES:
        for _, path, length in sorted(entries):
            path.unlink()
            total -= length
            if total <= MAX_CACHE_BYTES:
                break


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
