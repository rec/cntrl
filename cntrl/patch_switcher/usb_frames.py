"""Version-one binary USB frames, shared with firmware/include/cntrl/usb_frame.h."""

import struct
from enum import IntEnum
from zlib import crc32

from pydantic import BaseModel


class Kind(IntEnum):
    open = 1
    close = 2
    command = 3
    poll = 4
    setup = 16
    bundle = 17
    songs = 18
    font = 19
    history = 20
    movie = 21
    disk = 22
    text = 128
    data = 129
    error = 255


class Status(IntEnum):
    ok = 0
    lost = 1
    invalid = 2
    version = 3


class Frame(BaseModel, frozen=True):
    kind: Kind
    session: int
    sequence: int
    acknowledged: int = 0
    block_size: int = 0
    status: Status = Status.ok
    payload: bytes = b''

    def encode(self) -> bytes:
        header = HEADER.pack(
            MAGIC,
            1,
            self.kind,
            self.status,
            self.session,
            self.sequence,
            self.acknowledged,
            self.block_size,
            len(self.payload),
            crc32(self.payload),
            0,
        )
        return header[:-4] + struct.pack('<I', crc32(header[:-4])) + self.payload

    @classmethod
    def decode(cls, data: bytes) -> 'Frame':
        if len(data) < HEADER.size:
            raise ValueError('truncated binary USB header')
        (
            magic,
            version,
            kind,
            status,
            session,
            sequence,
            acknowledged,
            block,
            length,
            checksum,
            header_crc,
        ) = HEADER.unpack_from(data)
        if magic != MAGIC or crc32(data[: HEADER.size - 4]) != header_crc:
            raise ValueError('invalid binary USB header or header CRC')
        if version != 1:
            raise ValueError(f'unsupported binary USB version {version}; expected 1')
        payload = data[HEADER.size :]
        if len(payload) != length or crc32(payload) != checksum:
            raise ValueError('invalid binary USB payload length or CRC')
        return cls(
            kind=Kind(kind),
            status=Status(status),
            session=session,
            sequence=sequence,
            acknowledged=acknowledged,
            block_size=block,
            payload=payload,
        )


MAGIC = b'\x89CNTRL\r\n'
HEADER = struct.Struct('<8sBBH7I')
