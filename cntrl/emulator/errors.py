"""Expected emulator configuration, device and filesystem failures."""

from collections.abc import Generator
from contextlib import contextmanager


class EmulatorError(ValueError):
    """The emulator could not perform the requested operation."""


class EmulatorFileError(OSError):
    """An emulator input or output file could not be accessed."""


@contextmanager
def emulator_files() -> Generator[None]:
    try:
        yield
    except EmulatorFileError:
        raise
    except OSError as error:
        raise EmulatorFileError(str(error)) from error
