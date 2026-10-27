"""Portable song libraries with inline selections and live setup references."""

import struct
from pathlib import Path
from typing import Annotated
from zlib import crc32

import tomlkit
import tyro
from pydantic import BaseModel, ConfigDict, Field, model_validator

from . import transport
from .data_files import validate_name


class Patch(BaseModel, frozen=True):
    output: str
    channel: int = Field(ge=1, le=16)
    program: int = Field(ge=1, le=128)
    msb: int | None = Field(default=None, ge=0, le=127)
    lsb: int | None = Field(default=None, ge=0, le=127)
    volume: int | None = Field(default=None, ge=0, le=127)
    pan: int | None = Field(default=None, ge=-64, le=63)
    model_config = ConfigDict(extra='forbid')


class Reference(BaseModel, frozen=True):
    document: str
    instrument: str
    program: int = Field(ge=1, le=128)
    bank: int | None = Field(default=None, ge=0, le=0xFFFFFFFE)
    model_config = ConfigDict(extra='forbid')


class Song(BaseModel, frozen=True):
    id: int = Field(gt=0, lt=0xFFFFFFFF)
    name: str
    patch: Patch | None = None
    reference: Reference | None = None

    @model_validator(mode='after')
    def one_selection(self) -> 'Song':
        if (self.patch is None) == (self.reference is None):
            raise ValueError('supply exactly one of patch or reference')
        return self

    model_config = ConfigDict(extra='forbid')


class Setlist(BaseModel, frozen=True):
    id: int = Field(gt=0, lt=0xFFFFFFFF)
    name: str
    songs: list[int] = Field(default_factory=list)
    entry_ids: list[int] = Field(default_factory=list)
    model_config = ConfigDict(extra='forbid')


class Library(BaseModel, frozen=True):
    next_id: int | None = Field(default=None, ge=1, le=0xFFFFFFFF)
    songs: list[Song] = Field(default_factory=list)
    setlists: list[Setlist] = Field(default_factory=list)
    model_config = ConfigDict(extra='forbid')


class Copy(BaseModel, frozen=True):
    document: Annotated[Path, tyro.conf.Positional]
    port: Path
    overwrite: bool = False
    target: Path | None = None


class Download(BaseModel, frozen=True):
    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


def text(value: str, size: int) -> bytes:
    if not value or len(value) >= size or any(not 32 <= ord(c) <= 126 for c in value):
        raise ValueError(
            f'expected 1..{size - 1} printable ASCII characters: {value!r}'
        )
    return value.encode().ljust(size, b'\0')


def encode(library: Library) -> bytes:
    ids = [s.id for s in library.songs] + [s.id for s in library.setlists]
    for s in library.setlists:
        if s.entry_ids and len(s.entry_ids) != len(s.songs):
            raise ValueError('entry_ids must match songs')
        ids.extend(s.entry_ids)
        if any(i not in [s.id for s in library.songs] for i in s.songs):
            raise ValueError('setlist references an unknown song')
    if len(set(ids)) != len(ids):
        raise ValueError('song, setlist and occurrence IDs must be unique')
    next_id = max(ids, default=0) + 1
    if library.next_id is not None:
        if library.next_id < next_id:
            raise ValueError('next_id must exceed all existing IDs')
        next_id = library.next_id
    records = []
    for s in library.songs:
        if p := s.patch:
            selection = PATCH.pack(
                text(p.output, 32),
                p.channel - 1,
                p.program - 1,
                255 if p.msb is None else p.msb,
                255 if p.lsb is None else p.lsb,
                255 if p.volume is None else p.volume,
                255 if p.pan is None else p.pan + 64,
            )
            records.append(
                SONG.pack(
                    s.id, text(s.name, 64), bytes(64), bytes(32), selection, 0xFFFFFFFF
                )
            )
        elif r := s.reference:
            validate_name(r.document)
            selection = PATCH.pack(
                text('din', 32), 0, r.program - 1, 255, 255, 255, 255
            )
            records.append(
                SONG.pack(
                    s.id,
                    text(s.name, 64),
                    text(r.document, 64),
                    text(r.instrument, 32),
                    selection,
                    0xFFFFFFFF if r.bank is None else r.bank,
                )
            )
    sets = []
    for s in library.setlists:
        entries = []
        for i, song in enumerate(s.songs):
            if s.entry_ids:
                key = s.entry_ids[i]
            else:
                key = next_id
                next_id += 1
            entries.append(struct.pack('<II', key, song))
        sets.append(SET.pack(s.id, text(s.name, 64), len(entries)) + b''.join(entries))
    if next_id > 0xFFFFFFFF:
        raise ValueError('song IDs exhausted')
    payload = struct.pack(
        '<III', next_id, len(library.songs), len(library.setlists)
    ) + b''.join(records + sets)
    if len(payload) > 0xFFFFFFFF - 20:
        raise ValueError('song library exceeds the SD file format size')
    return payload


def decode(data: bytes) -> Library:
    if len(data) < 12:
        raise ValueError('invalid song library size')
    next_id, ns, nl = struct.unpack_from('<III', data)
    if ns > (len(data) - 12) // SONG.size:
        raise ValueError('invalid song library counts')

    def string(value: bytes) -> str:
        return value.split(b'\0')[0].decode('ascii')

    songs = []
    for i in range(ns):
        key, name, document, instrument, selection, bank = SONG.unpack_from(
            data, 12 + i * SONG.size
        )
        output, channel, program, msb, lsb, volume, pan = PATCH.unpack(selection)
        if document[0]:
            songs.append(
                Song(
                    id=key,
                    name=string(name),
                    reference=Reference(
                        document=string(document),
                        instrument=string(instrument),
                        program=program + 1,
                        bank=None if bank == 0xFFFFFFFF else bank,
                    ),
                )
            )
        else:
            songs.append(
                Song(
                    id=key,
                    name=string(name),
                    patch=Patch(
                        output=string(output),
                        channel=channel + 1,
                        program=program + 1,
                        msb=None if msb == 255 else msb,
                        lsb=None if lsb == 255 else lsb,
                        volume=None if volume == 255 else volume,
                        pan=None if pan == 255 else pan - 64,
                    ),
                )
            )
    sets = []
    offset = 12 + ns * SONG.size
    for _ in range(nl):
        if offset + SET.size > len(data):
            raise ValueError('truncated setlist')
        key, name, count = SET.unpack_from(data, offset)
        if count > (len(data) - offset - SET.size) // 8:
            raise ValueError('invalid setlist count')
        entries = [
            struct.unpack_from('<II', data, offset + SET.size + j * 8)
            for j in range(count)
        ]
        sets.append(
            Setlist(
                id=key,
                name=string(name),
                songs=[e[1] for e in entries],
                entry_ids=[e[0] for e in entries],
            )
        )
        offset += SET.size + count * 8
    if offset != len(data):
        raise ValueError('trailing song library data')
    result = Library(next_id=next_id, songs=songs, setlists=sets)
    encode(result)
    return result


def download(client: transport.SetupClient) -> bytes:
    response = client.request('CNTRL SONGS SIZE').split()
    if (
        len(response) != 4
        or response[:2] != ['CNTRL', 'SONGS']
        or not 12 <= int(response[2]) <= 0xFFFFFFFF - 20
    ):
        raise ValueError('invalid song library response')
    length = int(response[2])
    result = bytearray()
    while len(result) < length:
        response_line = client.request(f'CNTRL SONGS READ {len(result)}')
        if not response_line.startswith('CNTRL DATA '):
            raise ValueError(response_line)
        chunk = bytes.fromhex(response_line[11:])
        if not 1 <= len(chunk) <= 128:
            raise ValueError('invalid song library chunk')
        result.extend(chunk)
    if len(result) != length or crc32(result) != int(response[3], 16):
        raise ValueError('song library checksum mismatch')
    decode(bytes(result))
    return bytes(result)


def run(command: Copy | Download) -> None:
    from . import cli

    if isinstance(command, Copy):
        library = Library.model_validate(
            tomlkit.parse(command.document.read_text()).unwrap()
        )
        data = encode(library)
        with transport.SerialConnection(command.port) as connection:
            existing = decode(download(transport.SetupClient(connection)))
        if not command.overwrite and (existing.songs or existing.setlists):
            raise ValueError('song library exists; use --overwrite')
        references = sorted(
            {s.reference.document for s in library.songs if s.reference}
        )
        if references:
            if command.target is None:
                raise ValueError('reference imports require --target')
            # The companion setup documents are copied with their font assets.
            cli.run_setup(
                cli.Copy(
                    document=[
                        f'{n.replace("=", "==")}='
                        f'{command.document.parent / (n + ".toml")}'
                        for n in references
                    ],
                    target=[str(command.target)],
                    port=command.port,
                    overwrite=command.overwrite,
                )
            )
        with transport.SerialConnection(command.port) as connection:
            client = transport.SetupClient(connection)
            client.ok(
                f'CNTRL SONGS BEGIN {len(data)} {crc32(data):08x} '
                f'{int(command.overwrite)}'
            )
            for i in range(0, len(data), 128):
                client.ok('CNTRL SONGS DATA ' + data[i : i + 128].hex())
            client.ok('CNTRL SONGS END')
            if download(client) != data:
                raise ValueError('song library read-back mismatch')
        print('Song library copied and verified')
    else:
        command.directory.mkdir(parents=True, exist_ok=True)
        destination = command.directory / 'songs.toml'
        if destination.exists():
            raise ValueError(f'{destination}: already exists')
        with transport.SerialConnection(command.port) as connection:
            library = decode(download(transport.SetupClient(connection)))
        references = sorted(
            {s.reference.document for s in library.songs if s.reference}
        )
        if references:
            cli.run_setup(
                cli.Download(
                    names=references, directory=command.directory, port=command.port
                )
            )
        destination.write_text(tomlkit.dumps(library.model_dump(exclude_none=True)))
        print(destination)


PATCH = struct.Struct('<32s6B')
SONG = struct.Struct('<I64s64s32s38sI')
SET = struct.Struct('<I64sI')
