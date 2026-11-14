"""Build the indexed setup image and its RAM2 residency plan."""

import struct
from functools import cached_property
from typing import cast
from zlib import crc32

from pydantic import BaseModel, Field, model_validator
from reccy.configuration import units

from . import binary_format, compiler, data_files, display, models, resolved


class SetupBundle(BaseModel, frozen=True):
    setups: dict[str, bytes] = Field(min_length=1, max_length=65535)
    page_ram2: list[int]
    memory: models.TargetMemory
    memory_margin: units.Bytes = Field(default=16192, ge=0)

    @model_validator(mode='after')
    def feasible(self) -> 'SetupBundle':
        if len(self.page_ram2) != len(self.setups):
            raise ValueError('one page RAM estimate is required per setup')
        for name in self.names:
            data_files.validate_name(name)
        if len({n.lower() for n in self.names}) != len(self.names):
            raise ValueError('duplicate setup name')
        if self.total_free_ram2() <= 0:
            raise ValueError('non-setup RAM2 use and memory margin exhaust RAM2')
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
        # Movie keys and filenames are part of the compiled setup sizes. Full
        # movies stay on SD; there is no size-dependent RAM2 allocation.
        # Names/pointers, validated views/cache entries, and packed-table prefix.
        metadata = (
            len(self.names) * (64 + 4 + 108 + 85) + 2 + 4 * (len(self.names) - 1) + 32
        )
        active = max(self.page_ram2)
        return self.memory.runtime + metadata + active

    def total_free_ram2(self) -> int:
        return self.memory.capacity - self.memory_margin - self.total_ram2_needs()

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
                self.page_ram2[i],
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
                self.memory.runtime,
                max(self.page_ram2),
                self.total_ram2_needs(),
                self.total_setup_ram,
                self.total_free_ram2(),
            )
            + body
        )


def build(
    setups: dict[str, bytes],
    sources: dict[str, object],
    memory: models.TargetMemory,
    memory_margin: int = 16192,
) -> SetupBundle:
    ordered = sorted(setups, key=str.lower)
    page_ram2: list[int] = []
    for name in ordered:
        settings = models.Setup.model_validate(sources[name])
        data = binary_format.core(setups[name])
        # Size-only callers can supply opaque payloads. Compiled setups carry
        # the authoritative estimate, including imported routing graphs.
        if not data.startswith(b'CSW1'):
            page_ram2.append(sum(p.ram2_bytes for p in settings.pages.values()))
            continue
        instruments, cycles, patches = struct.unpack_from('<HHH', data, 12)
        metadata = 76 + instruments * 68 + cycles * 68 + patches * 82
        page_ram2.append(
            struct.unpack_from('<I', data, metadata + compiler.METADATA.size - 8)[0]
        )
    return SetupBundle(
        setups={
            n: binary_format.wrap(d) if binary_format.core(d).startswith(b'CSW1') else d
            for n, d in setups.items()
        },
        page_ram2=page_ram2,
        memory=memory,
        memory_margin=memory_margin,
    )


def reindex(setups: dict[str, bytes]) -> dict[str, bytes]:
    """Relink downloaded/history images when their sorted bundle table changes."""
    indices = {n.lower(): i for i, n in enumerate(sorted(setups, key=str.lower))}
    signature = crc32(b''.join(n.encode('ascii').ljust(64, b'\0') for n in indices))
    result: dict[str, bytes] = {}
    for name, original in setups.items():
        core = binary_format.core(original)
        data = bytearray(core)
        graph = resolved.graph_offset(core)
        source = display.read_source(core, graph)
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
            destination = indices[document.lower()]
            if struct.unpack_from('<H', data, at + 4)[0] != destination:
                binary_format.require_structural_edit(original)
            struct.pack_into('<H', data, at + 4, destination)
        ni, nc, np = compiler.HEADER.unpack_from(data)[4:7]
        meta = 76 + ni * 68 + nc * 68 + np * 82
        if (
            struct.unpack_from('<I', data, meta + compiler.METADATA.size - 4)[0]
            != signature
        ):
            binary_format.require_structural_edit(original)
        struct.pack_into('<I', data, meta + compiler.METADATA.size - 4, signature)
        struct.pack_into('<I', data, 20, crc32(data[28:]))
        result[name] = binary_format.replace_core(original, bytes(data))
    return result


def unpack(data: bytes) -> dict[str, bytes]:
    if len(data) < HEADER.size:
        raise ValueError('truncated setup bundle')
    magic, version, count, length, checksum, *_ = HEADER.unpack_from(data)
    if (
        magic != b'CSTB'
        or version != 1
        or not count
        or length != len(data)
        or crc32(data[HEADER.size :]) != checksum
    ):
        raise ValueError('invalid setup bundle')
    table = HEADER.size + count * ENTRY.size
    cursor = table + 2 + 4 * (count - 1)
    if cursor > len(data) or struct.unpack_from('<H', data, table)[0] != count:
        raise ValueError('invalid setup bundle table')
    start = cursor
    result: dict[str, bytes] = {}
    previous = ''
    for i in range(count):
        record = HEADER.size + i * ENTRY.size
        encoded, size, _, prefill = ENTRY.unpack_from(data, record)
        name, separator, padding = encoded.partition(b'\0')
        if (
            not separator
            or padding.strip(b'\0')
            or prefill > 1
            or any(data[record + 73 : record + 76])
        ):
            raise ValueError('invalid setup bundle entry')
        name = data_files.validate_name(name.decode('ascii'))
        if name.lower() <= previous or size > len(data) - cursor:
            raise ValueError('invalid setup bundle names or lengths')
        previous = name.lower()
        if (
            i
            and struct.unpack_from('<I', data, table + 2 + (i - 1) * 4)[0]
            != cursor - start
        ):
            raise ValueError('invalid setup bundle offsets')
        if size:
            result[name] = data[cursor : cursor + size]
        cursor += size
    if cursor != len(data) or cursor - start != HEADER.unpack_from(data)[9]:
        raise ValueError('invalid setup bundle offsets')
    return result


def memory_from_bundle(data: bytes) -> models.TargetMemory:
    """Recover the target memory descriptor encoded in an existing bundle."""
    unpack(data)
    header = HEADER.unpack_from(data)
    return models.TargetMemory(
        capacity=header[5] + header[8] + header[10], runtime=header[6]
    )


HEADER = struct.Struct('<4sHH8I')
ENTRY = struct.Struct('<64sIIB3x')
