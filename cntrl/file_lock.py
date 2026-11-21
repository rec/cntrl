"""Exclusive writer locks for local state directories."""

import sys
from collections.abc import Generator
from contextlib import contextmanager
from errno import EACCES, EAGAIN
from pathlib import Path


class WriterLockHeld(ValueError):
    """Another operation holds the write lock for this directory."""


@contextmanager
def writer_lock(root: Path, *, operation: str) -> Generator[None]:
    """Keep the lock file in place so every writer locks the same file."""
    root.mkdir(parents=True, exist_ok=True)
    with (root / 'writer.lock').open('a+b') as lock:
        try:
            if sys.platform == 'win32':
                import msvcrt

                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            if error.errno in (EACCES, EAGAIN):
                raise WriterLockHeld(
                    f'{root}: another {operation} is in progress'
                ) from error
            raise
        yield
