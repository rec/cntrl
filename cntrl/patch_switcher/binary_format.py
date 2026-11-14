"""Length-delimited persistent documents; development formats are never releases."""

import struct
from enum import IntEnum
from zlib import crc32

from pydantic import BaseModel, Field


class Kind(IntEnum):
    setup = 1
    bundle = 2
    songs = 3


class Record(BaseModel, frozen=True):
    tag: int = Field(ge=1, le=65535)
    required: bool = False
    data: bytes


class Document(BaseModel, frozen=True):
    kind: Kind
    records: list[Record]

    def pack(self) -> bytes:
        payload = b''.join(
            RECORD.pack(r.tag, int(r.required), len(r.data)) + r.data
            for r in self.records
        )
        if len(payload) > MAX_PAYLOAD:
            raise ValueError('persistent document exceeds format size limit')
        header = HEADER.pack(
            MAGIC, DEVELOPMENT_FORMAT, self.kind, HEADER.size, 0, len(payload), 0
        )
        result = header + payload
        # Validation also applies to hand-constructed documents.
        result = result[:16] + struct.pack('<I', crc32(result)) + result[20:]
        unpack(result, self.kind)
        return result


def unpack(data: bytes, kind: Kind) -> Document:
    if len(data) < HEADER.size:
        raise ValueError('truncated persistent document header')
    magic, version, actual_kind, header_size, flags, length, checksum = (
        HEADER.unpack_from(data)
    )
    if magic != MAGIC:
        raise ValueError('invalid persistent document magic')
    if version not in (0x802C, 0x802D, DEVELOPMENT_FORMAT):
        raise ValueError(f'unsupported persistent format {format_name(version)}')
    if actual_kind != kind:
        raise ValueError('persistent document artifact kind mismatch')
    if header_size != HEADER.size or flags:
        raise ValueError('unsupported persistent document header')
    if length > MAX_PAYLOAD or length != len(data) - HEADER.size:
        raise ValueError('invalid persistent document length')
    if crc32(data[:16] + bytes(4) + data[20:]) != checksum:
        raise ValueError('persistent document checksum mismatch')
    records: list[Record] = []
    cursor = HEADER.size
    core_found = False
    while cursor < len(data):
        if len(records) >= MAX_RECORDS:
            raise ValueError('persistent document exceeds record count limit')
        if len(data) - cursor < RECORD.size:
            raise ValueError('truncated persistent record header')
        tag, flags, size = RECORD.unpack_from(data, cursor)
        cursor += RECORD.size
        if not tag or flags & ~REQUIRED or size > len(data) - cursor:
            raise ValueError('invalid persistent record')
        if tag == CORE:
            if core_found or flags != REQUIRED or records:
                raise ValueError('core must be the first, required, singleton record')
            core_found = True
        elif flags & REQUIRED:
            raise ValueError(f'unsupported required persistent record {tag}')
        records.append(
            Record(tag=tag, required=bool(flags), data=data[cursor : cursor + size])
        )
        cursor += size
    if not core_found:
        raise ValueError('missing persistent core record')
    return Document(kind=kind, records=records)


def wrap(data: bytes, kind: Kind = Kind.setup) -> bytes:
    if data.startswith(MAGIC):
        return unpack(data, kind).pack()
    return Document(
        kind=kind, records=[Record(tag=CORE, required=True, data=data)]
    ).pack()


def core(data: bytes, kind: Kind = Kind.setup) -> bytes:
    """Legacy importers use bare cores; new persistent files use the envelope."""
    return unpack(data, kind).records[0].data if data.startswith(MAGIC) else data


def core_offset(data: bytes, kind: Kind = Kind.setup) -> int:
    if not data.startswith(MAGIC):
        return 0
    unpack(data, kind)
    return HEADER.size + RECORD.size


def replace_core(data: bytes, replacement: bytes, kind: Kind = Kind.setup) -> bytes:
    if not data.startswith(MAGIC):
        return replacement
    document = unpack(data, kind)
    return Document(
        kind=kind,
        records=[
            Record(tag=CORE, required=True, data=replacement),
            *document.records[1:],
        ],
    ).pack()


def require_structural_edit(data: bytes, kind: Kind = Kind.setup) -> None:
    if data.startswith(MAGIC) and len(unpack(data, kind).records) != 1:
        raise ValueError('cannot remap references while opaque metadata is present')


def format_name(version: int) -> str:
    return f'd{version & 0x7FFF}' if version & 0x8000 else f'f{version}'


def identify(data: bytes) -> str:
    """Describe an archive entry without claiming its contents are valid."""
    if data.startswith(MAGIC) and len(data) >= HEADER.size:
        return format_name(HEADER.unpack_from(data)[1])
    if data.startswith(b'CSW1') and len(data) >= 6:
        return f'd{struct.unpack_from("<H", data, 4)[0]}'
    if data.startswith(b'CSTB') and len(data) >= 6:
        return f'bundle-{struct.unpack_from("<H", data, 4)[0]}'
    for magic, name in ARCHIVE_KINDS:
        if data.startswith(magic):
            return name
    return 'opaque'


MAGIC = b'CNBF'
DEVELOPMENT_FORMAT = 0x8000 | 46
HEADER = struct.Struct('<4sHHHHII')
RECORD = struct.Struct('<HHI')
CORE = 1
REQUIRED = 1
MAX_PAYLOAD = 16 * 1024 * 1024
MAX_RECORDS = 4096
SETUP_VERSIONS = (36, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47)
ARCHIVE_KINDS = (
    (b'MThd', 'SMF'),
    (b'CFN2', 'CFN2'),
    (b'CNSA', 'CNSA'),
    (b'CSG5', 'song-journal-5'),
    (b'CSG6', 'song-journal-6'),
    (b'CREV', 'revision-journal'),
    (b'CNST', 'setup-journal'),
    (b'CSEL', 'selection-journal'),
    (b'CDEP', 'activation-journal'),
)
