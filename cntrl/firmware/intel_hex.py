"""Read and write Intel HEX images with validated checksums and addresses."""

import hashlib
from collections import Counter
from pathlib import Path

from pydantic import BaseModel


class AddressRange(BaseModel, frozen=True):
    """A populated address interval, with an exclusive end."""

    start: int
    end: int


class FirmwareImage(BaseModel, frozen=True):
    origin: int
    entry: int | None
    data: bytes
    regions: list[AddressRange]
    record_counts: dict[int, int]
    sha256: str


def read_hex(path: Path) -> FirmwareImage:
    """Validate Intel HEX records and decode addresses, filling gaps with 0xff."""
    source = path.read_bytes()
    memory: dict[int, int] = {}
    records: Counter[int] = Counter()
    base = 0
    entry: int | None = None
    finished = False
    for number, line in enumerate(source.decode('ascii').splitlines(), 1):
        if finished or not line.startswith(':'):
            raise ValueError(f'line {number}: invalid record or data after EOF')
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5:
            raise ValueError(f'line {number}: incorrect record length')
        if sum(record) % 256:
            raise ValueError(f'line {number}: invalid checksum')
        address = int.from_bytes(record[1:3], 'big')
        kind = record[3]
        payload = record[4:-1]
        records[kind] += 1
        if kind == 0:
            for index, byte in enumerate(payload):
                location = base + address + index
                if location in memory:
                    raise ValueError(
                        f'line {number}: overlapping data at {location:#x}'
                    )
                memory[location] = byte
        elif kind in (2, 4) and len(payload) == 2 and address == 0:
            base = int.from_bytes(payload, 'big') << (4 if kind == 2 else 16)
        elif kind == 5 and len(payload) == 4 and address == 0:
            entry = int.from_bytes(payload, 'big')
        elif kind == 1 and not payload and address == 0:
            finished = True
        else:
            raise ValueError(f'line {number}: unsupported or malformed record {kind}')
    if not finished or not memory:
        raise ValueError('image must contain data and an EOF record')

    addresses = sorted(memory)
    regions: list[AddressRange] = []
    start = previous = addresses[0]
    for address in addresses[1:]:
        if address != previous + 1:
            regions.append(AddressRange(start=start, end=previous + 1))
            start = address
        previous = address
    regions.append(AddressRange(start=start, end=previous + 1))
    origin = addresses[0]
    return FirmwareImage(
        origin=origin,
        entry=entry,
        data=bytes(memory.get(a, 0xFF) for a in range(origin, addresses[-1] + 1)),
        regions=regions,
        record_counts=dict(records),
        sha256=hashlib.sha256(source).hexdigest(),
    )


def encode_hex(data: bytes, origin: int, entry: int | None) -> str:
    if not data or origin < 0 or origin + len(data) > 0x100000000:
        raise ValueError('image must contain data within 32-bit addresses')
    if entry is not None and not 0 <= entry <= 0xFFFFFFFF:
        raise ValueError('entry must be a 32-bit address')
    lines: list[str] = []
    upper = -1
    offset = 0
    while offset < len(data):
        address = origin + offset
        if address >> 16 != upper:
            upper = address >> 16
            record = bytes([2, 0, 0, 4]) + upper.to_bytes(2, 'big')
            lines.append(':' + (record + bytes([-sum(record) & 255])).hex().upper())
        amount = min(16, 65536 - (address & 65535))
        chunk = data[offset : offset + amount]
        record = bytes([len(chunk)]) + (address & 65535).to_bytes(2, 'big')
        record += b'\x00' + chunk
        lines.append(':' + (record + bytes([-sum(record) & 255])).hex().upper())
        offset += len(chunk)
    if entry is not None:
        record = bytes([4, 0, 0, 5]) + entry.to_bytes(4, 'big')
        lines.append(':' + (record + bytes([-sum(record) & 255])).hex().upper())
    lines.append(':00000001FF')
    return '\n'.join(lines) + '\n'
