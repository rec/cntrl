"""Read the pedal's SD-card identity and filesystem information over USB."""

import sys
import termios
from collections.abc import Callable
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel, Field

from ....patch_switcher.transport import SerialConnection


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
    card_type: str
    manufacturer_id: str
    oem_id: str
    product: str
    revision: str
    serial_number: str
    manufactured: str
    filesystem: str
    allocation_unit_bytes: int
    usable_bytes: int
    free_bytes: int | None
    used_bytes: int | None
    files_bytes: int = 0
    estimated_file_allocation_bytes: int = 0
    other_allocation_bytes: int = 0
    directories: list[DirectoryTotal] = Field(default_factory=list)
    entries: list[CardEntry] = Field(default_factory=list)


def parse_storage_info(response: str) -> StorageInfo:
    fields = response.split()
    if len(fields) != 9 or fields[:2] != ['CNTRL', 'STORAGE']:
        raise ValueError(response)
    sectors, kind, identity, fat, cluster_bytes, clusters, free = fields[2:]
    cid = bytes.fromhex(identity)
    if len(cid) != 16:
        raise ValueError('Expected a 16-byte SD identification register')
    capacity = int(sectors) * 512
    unit, count = int(cluster_bytes), int(clusters)
    available = None if free == '-' else int(free)
    if available is not None and not 0 <= available <= count:
        raise ValueError('Invalid free cluster count')
    if kind not in ('1', '2', '3') or fat not in ('16', '32', '64'):
        raise ValueError('Unsupported card or filesystem type')
    card_type = {'1': 'SD v1', '2': 'SD v2', '3': 'SDHC'}[kind]
    if kind == '3' and capacity > 32 * 1024**3:
        card_type = 'SDXC'
    return StorageInfo(
        capacity_bytes=capacity,
        card_type=card_type,
        manufacturer_id=f'0x{cid[0]:02X}',
        oem_id=cid[1:3].decode('ascii'),
        product=cid[3:8].decode('ascii'),
        revision=f'{cid[8] >> 4}.{cid[8] & 15}',
        serial_number=f'0x{int.from_bytes(cid[9:13], "big"):08X}',
        manufactured=(
            f'{2000 + ((cid[13] & 15) << 4) + (cid[14] >> 4)}-{cid[14] & 15:02d}'
        ),
        filesystem={'16': 'FAT16', '32': 'FAT32', '64': 'exFAT'}[fat],
        allocation_unit_bytes=unit,
        usable_bytes=count * unit,
        free_bytes=None if available is None else available * unit,
        used_bytes=None if available is None else (count - available) * unit,
    )


def read_storage_report(read_line: Callable[[], str]) -> StorageInfo:
    info = parse_storage_info(read_line())
    paths: dict[int, str] = {0: ''}
    totals: dict[str, list[int]] = {'/': [0, 0, 0]}
    entries: list[CardEntry] = []
    unit = info.allocation_unit_bytes
    while True:
        response = read_line()
        if response == f'CNTRL STORAGE END {len(entries)}':
            break
        fields = response.split()
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
        return read_storage_report(lambda: connection.line(limit=1024))


def main() -> None:
    options = tyro.cli(Options)
    try:
        result = query_storage_info(options.port)
        report = tomlkit.dumps(result.model_dump(exclude_none=True))
        if options.output is None:
            print(report, end='')
        else:
            options.output.write_text(report)
    except (OSError, ValueError, termios.error) as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
