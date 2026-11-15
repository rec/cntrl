"""Device-free child process for exercising qualification supervision."""

import sys
import time
from enum import StrEnum, auto
from pathlib import Path

import tyro
from pydantic import BaseModel


class Behavior(StrEnum):
    passed = auto()
    failed = auto()
    hung = auto()
    malformed = auto()


class Options(BaseModel, frozen=True):
    directory: Path
    behavior: Behavior


def main(options: Options) -> None:
    (options.directory / 'heartbeat.txt').write_text('started')
    (options.directory / 'evidence.txt').write_text('retained bytes')
    path = options.directory / 'result.toml'
    if options.behavior == Behavior.malformed:
        path.write_text('[')
        return
    path.write_text('success = true\n')
    print('ready', flush=True)
    if options.behavior == Behavior.failed:
        sys.exit('simulated worker failure after report publication')
    if options.behavior == Behavior.hung:
        time.sleep(60)


if __name__ == '__main__':
    main(tyro.cli(Options))
