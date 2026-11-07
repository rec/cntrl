"""Build the indexed setup image and its RAM2 residency plan."""

import struct
from functools import cached_property
from typing import cast
from zlib import crc32

from pydantic import BaseModel, Field, model_validator
from reccy.configuration import units

from . import compiler, data_files, display, models, resolved


class SetupBundle(BaseModel, frozen=True):
    setups: dict[str, bytes] = Field(min_length=1, max_length=65535)
    app_ram2: list[int]
    page_ram2: list[int]
    memory_margin: units.Bytes = Field(default=16192, ge=0, le=524288)
    # Includes fixed RAM2, temporary document buffers, fonts and runtime heaps.
    # This is an estimate until the target reports measured high-water usage.
    cntrl_ram2: units.Bytes = Field(default=294912, ge=0, le=524288)

    @model_validator(mode='after')
    def feasible(self) -> 'SetupBundle':
        if len(self.app_ram2) != len(self.setups) or len(self.page_ram2) != len(
            self.setups
        ):
            raise ValueError('one app/page RAM estimate is required per setup')
        for name in self.names:
            data_files.validate_name(name)
        if len({n.lower() for n in self.names}) != len(self.names):
            raise ValueError('duplicate setup name')
        if self.total_free_ram2() <= 0:
            raise ValueError('non-setup RAM2 use and memory margin exhaust RAM2')
        if any(n > self.total_free_ram2() for n in self.setup_sizes):
            raise ValueError('a setup exceeds the RAM2 cache capacity')
        return self

    @cached_property
    def names(self) -> list[str]:
        return sorted(self.setups, key=str.lower)

    @cached_property
    def setup_sizes(self) -> list[int]:
        return [len(self.setups[n]) for n in self.names]

    @cached_property
    def total_setup_ram(self) -> int:
        return sum(self.setup_sizes)

    def total_ram2_needs(self) -> int:
        # Names/pointers, validated views/cache entries, and packed-table prefix.
        metadata = (
            len(self.names) * (64 + 4 + 80 + 85) + 2 + 4 * (len(self.names) - 1) + 32
        )
        active = max(a + p for a, p in zip(self.app_ram2, self.page_ram2, strict=True))
        return self.cntrl_ram2 + metadata + active

    def total_free_ram2(self) -> int:
        return 524288 - self.memory_margin - self.total_ram2_needs()

    @cached_property
    def prefill(self) -> list[int]:
        if self.total_setup_ram <= self.total_free_ram2():
            return list(range(len(self.names)))
        # Exact subset sum, rather than a largest-first heuristic. Retain earlier
        # names when several subsets occupy the same number of bytes.
        mask = (1 << (self.total_free_ram2() + 1)) - 1
        reachable = 1
        previous: list[int] = []
        for size in self.setup_sizes:
            previous.append(reachable)
            reachable |= (reachable << size) & mask
        remaining = reachable.bit_length() - 1
        chosen: list[int] = []
        for i in reversed(range(len(self.names))):
            if not (previous[i] >> remaining) & 1:
                chosen.append(i)
                remaining -= self.setup_sizes[i]
        return sorted(chosen)

    def pack(self) -> bytes:
        count = len(self.names)
        table = struct.pack('<H', count)
        offset = 0
        for size in self.setup_sizes[:-1]:
            offset += size
            table += struct.pack('<I', offset)
        payload = table + b''.join(self.setups[n] for n in self.names)
        entries = b''.join(
            ENTRY.pack(
                n.encode('ascii'),
                s,
                self.app_ram2[i] + self.page_ram2[i],
                int(i in self.prefill),
            )
            for i, (n, s) in enumerate(zip(self.names, self.setup_sizes, strict=True))
        )
        body = entries + payload
        return (
            HEADER.pack(
                b'CSTB',
                1,
                count,
                HEADER.size + len(body),
                crc32(body),
                self.memory_margin,
                self.cntrl_ram2,
                max(a + p for a, p in zip(self.app_ram2, self.page_ram2, strict=True)),
                self.total_ram2_needs(),
                self.total_setup_ram,
                self.total_free_ram2(),
            )
            + body
        )


def build(
    setups: dict[str, bytes], sources: dict[str, object], memory_margin: int = 16192
) -> SetupBundle:
    ordered = sorted(setups, key=str.lower)
    settings = [models.Setup.model_validate(sources[n]) for n in ordered]
    return SetupBundle(
        setups=setups,
        app_ram2=[sum(a.ram2_bytes for a in s.apps.values()) for s in settings],
        page_ram2=[sum(p.ram2_bytes for p in s.pages) for s in settings],
        memory_margin=memory_margin,
    )


def reindex(setups: dict[str, bytes]) -> dict[str, bytes]:
    """Relink downloaded/history images when their sorted bundle table changes."""
    indices = {n.lower(): i for i, n in enumerate(sorted(setups, key=str.lower))}
    signature = crc32(b''.join(n.encode('ascii').ljust(64, b'\0') for n in indices))
    result: dict[str, bytes] = {}
    for name, original in setups.items():
        data = bytearray(original)
        graph = resolved.graph_offset(original)
        source = display.read_source(original, graph)
        metadata = cast(dict[str, dict[str, object]], source['patch_metadata'])
        count = struct.unpack_from('<H', data, graph + 8)[0]
        for i in range(count):
            at = (
                graph
                + struct.unpack_from('<I', data, graph + compiler.GRAPH.size + i * 4)[0]
            )
            if data[at] != 6 or struct.unpack_from('<H', data, at + 4)[0] == 65535:
                continue
            document = str(metadata[str(i)]['_document'])
            if document.lower() not in indices:
                raise ValueError(f'{name}: referenced setup {document!r} is absent')
            struct.pack_into('<H', data, at + 4, indices[document.lower()])
        ni, nc, np = compiler.HEADER.unpack_from(data)[4:7]
        meta = 76 + ni * 68 + nc * 68 + np * 82
        struct.pack_into('<I', data, meta + compiler.METADATA.size - 4, signature)
        struct.pack_into('<I', data, 20, crc32(data[28:]))
        result[name] = bytes(data)
    return result


def unpack(data: bytes) -> dict[str, bytes]:
    if len(data) < HEADER.size:
        raise ValueError('truncated setup bundle')
    magic, version, count, length, checksum, *_ = HEADER.unpack_from(data)
    if (
        magic != b'CSTB'
        or version != 1
        or length != len(data)
        or crc32(data[HEADER.size :]) != checksum
    ):
        raise ValueError('invalid setup bundle')
    cursor = HEADER.size + count * ENTRY.size + 2 + 4 * (count - 1)
    result: dict[str, bytes] = {}
    for i in range(count):
        name, size, _, _ = ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
        if size:
            result[name.split(b'\0')[0].decode('ascii')] = data[cursor : cursor + size]
        cursor += size
    if cursor != len(data):
        raise ValueError('invalid setup bundle offsets')
    return result


HEADER = struct.Struct('<4sHH8I')
ENTRY = struct.Struct('<64sIIB3x')
