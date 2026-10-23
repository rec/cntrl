"""Read the pedal's SD-card identity and filesystem information over USB."""

import sys
from pathlib import Path

import tyro
from pydantic import BaseModel

from ....patch_switcher.transport import SerialConnection


class Options(BaseModel, frozen=True):
    port: Path
    # May pause the pedal while the filesystem allocation table is scanned.
    scan_free_space: bool = False


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


def query_storage_info(port: Path, scan_free_space: bool = False) -> StorageInfo:
    with SerialConnection(port) as connection:
        connection.write(
            b'CNTRL STORAGE SCAN\n' if scan_free_space else b'CNTRL STORAGE\n'
        )
        return parse_storage_info(connection.line())


def main() -> None:
    options = tyro.cli(Options)
    try:
        result = query_storage_info(options.port, options.scan_free_space)
    except (OSError, ValueError) as error:
        sys.exit(str(error))
    print(result.model_dump_json(indent=2))


if __name__ == '__main__':
    main()
