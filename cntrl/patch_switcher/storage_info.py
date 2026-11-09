"""Read storage capacity, optional implementation details, and directory entries."""

import sys
from collections.abc import Callable
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel, Field

from .transport import SerialConnection


class Options(BaseModel, frozen=True):
    port: Path
    output: Path | None = None


class CardEntry(BaseModel, frozen=True):
    path: str
    directory: bool
    size_bytes: int
    estimated_allocation_bytes: int


class DirectoryTotal(BaseModel, frozen=True):
    path: str
    files: int
    size_bytes: int
    estimated_allocation_bytes: int


class StorageInfo(BaseModel, frozen=True):
    capacity_bytes: int
    allocation_unit_bytes: int
    usable_bytes: int
    free_bytes: int | None
    used_bytes: int | None
    details: dict[str, str] = Field(default_factory=dict)
    files_bytes: int = 0
    estimated_file_allocation_bytes: int = 0
    other_allocation_bytes: int = 0
    directories: list[DirectoryTotal] = Field(default_factory=list)
    entries: list[CardEntry] = Field(default_factory=list)


def parse_storage_info(response: str) -> StorageInfo:
    fields = response.split()
    if len(fields) != 6 or fields[:2] != ['CNTRL', 'STORAGE']:
        raise ValueError(response)
    capacity, usable, free, unit = fields[2:]
    capacity, usable, unit = int(capacity), int(usable), int(unit)
    available = None if free == '-' else int(free)
    if not 0 <= usable <= capacity or unit <= 0:
        raise ValueError('Invalid storage capacity or allocation unit')
    if available is not None and not 0 <= available <= usable:
        raise ValueError('Invalid free byte count')
    return StorageInfo(
        capacity_bytes=capacity,
        allocation_unit_bytes=unit,
        usable_bytes=usable,
        free_bytes=available,
        used_bytes=None if available is None else usable - available,
    )


def read_storage_report(read_line: Callable[[], str]) -> StorageInfo:
    info = parse_storage_info(read_line())
    paths: dict[int, str] = {0: ''}
    totals: dict[str, list[int]] = {'/': [0, 0, 0]}
    entries: list[CardEntry] = []
    details: dict[str, str] = {}
    unit = info.allocation_unit_bytes
    while True:
        response = read_line()
        if response == f'CNTRL STORAGE END {len(entries)}':
            break
        fields = response.split()
        if len(fields) == 4 and fields[:2] == ['CNTRL', 'DETAIL']:
            key, value = (bytes.fromhex(x).decode('utf-8') for x in fields[2:])
            if entries or not key or key in details:
                raise ValueError('Invalid storage detail order or duplicate key')
            details[key] = value
            continue
        if len(fields) != 7 or fields[:2] != ['CNTRL', 'ENTRY']:
            raise ValueError(response)
        identifier, parent, kind, size, encoded = fields[2:]
        if int(identifier) != len(entries) + 1 or int(parent) not in paths:
            raise ValueError('Invalid directory listing order')
        name = bytes.fromhex(encoded).decode('utf-8')
        if not name or name in ('.', '..') or '/' in name or kind not in ('D', 'F'):
            raise ValueError('Invalid directory entry')
        path = paths[int(parent)] + '/' + name
        length = int(size, 16)
        allocation = ((length + unit - 1) // unit) * unit
        entries.append(
            CardEntry(
                path=path,
                directory=kind == 'D',
                size_bytes=length,
                estimated_allocation_bytes=allocation,
            )
        )
        if kind == 'D':
            paths[int(identifier)] = path
            totals[path] = [0, 0, 0]
        else:
            directory = paths[int(parent)]
            while True:
                total = totals[directory or '/']
                total[0] += 1
                total[1] += length
                total[2] += allocation
                if not directory:
                    break
                directory = directory.rsplit('/', 1)[0]
    total = totals['/']
    return info.model_copy(
        update={
            'details': details,
            'entries': sorted(entries, key=lambda e: e.path),
            'directories': [
                DirectoryTotal(
                    path=p, files=t[0], size_bytes=t[1], estimated_allocation_bytes=t[2]
                )
                for p, t in sorted(totals.items(), key=lambda t: -t[1][2])
            ],
            'files_bytes': total[1],
            'estimated_file_allocation_bytes': total[2],
            'other_allocation_bytes': (info.used_bytes or 0) - total[2],
        }
    )


def query_storage_info(port: Path) -> StorageInfo:
    with SerialConnection(port) as connection:
        connection.write(b'CNTRL STORAGE SCAN\n')

        def next_line() -> str:
            response = connection.line(limit=1024)
            if not response.startswith(('CNTRL STORAGE END', 'CNTRL ERROR')):
                connection.write(b'CNTRL STORAGE NEXT\n')
            return response

        return read_storage_report(next_line)


def main() -> None:
    options = tyro.cli(Options)
    try:
        result = query_storage_info(options.port)
        report = tomlkit.dumps(result.model_dump(exclude_none=True))
        if options.output is None:
            print(report, end='')
        else:
            options.output.write_text(report)
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError) as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
