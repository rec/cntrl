"""Named setup snapshots and semantic comparisons through the existing USB link."""

import struct
import sys
import time
from enum import StrEnum, auto
from pathlib import Path
from typing import Annotated
from zlib import crc32

import tomlkit
import tyro
from pydantic import BaseModel, Field

from . import data_files, display, resolved, setup_bundle, transport
from .usb_frames import Kind


class Command(StrEnum):
    list = auto()
    save = auto()
    download = auto()
    compare = auto()
    restore = auto()
    delete = auto()
    clear = auto()


class History(BaseModel, frozen=True):
    """List, save, download, compare, restore or delete complete setup revisions."""

    name: Annotated[str, tyro.conf.Positional]
    port: Path
    action: Annotated[Command, tyro.conf.Positional] = Command.list
    revision: int = 0
    label: str = ''
    output: Path | None = None
    yes: bool = False


class Revision(BaseModel, frozen=True):
    number: int = Field(gt=0, lt=2**64 - 1)
    modified: int = Field(ge=0, le=0xFFFFFFFF)
    length: int = Field(ge=116, le=0xFFFFFFFF)
    label: str = ''


def revisions(client: transport.SetupClient, name: str) -> list[Revision]:
    """Read every valid snapshot and its original number, time, and label."""
    encoded = data_files.validate_name(name).encode('ascii').hex()
    before = 2**64 - 1
    result = []
    while True:
        response = client.request(f'CNTRL HISTORY LIST {encoded} {before}')
        if response == 'CNTRL END':
            return result
        fields = response.split()
        if len(fields) != 6 or fields[:2] != ['CNTRL', 'REVISION']:
            raise ValueError(f'invalid revision response: {response}')
        revision = Revision(
            number=int(fields[2]),
            modified=int(fields[3]),
            length=int(fields[4]),
            label='' if fields[5] == '-' else bytes.fromhex(fields[5]).decode('ascii'),
        )
        if revision.number >= before:
            raise ValueError('invalid revision order')
        result.append(revision)
        before = revision.number


def normalize_revision(name: str, data: bytes) -> tuple[list[str], bytes]:
    """Give a historical graph its own name table, independent of the live bundle."""
    names = {data_files.validate_name(name)}
    source = display.read_source(data, resolved.graph_offset(data))
    metadata = source.get('patch_metadata', {})
    if isinstance(metadata, dict):
        for value in metadata.values():
            if isinstance(value, dict) and isinstance(
                document := value.get('_document'), str
            ):
                names.add(data_files.validate_name(document))
    ordered = sorted(names, key=str.lower)
    return ordered, setup_bundle.reindex({n: data for n in ordered})[name]


def pack_revision(name: str, revision: Revision, data: bytes) -> bytes:
    names, normalized = normalize_revision(name, data)
    table = b''.join(n.encode('ascii').ljust(64, b'\0') for n in names)
    label = (
        data_files.validate_name(revision.label).encode('ascii')
        if revision.label
        else b''
    )
    payload = table + normalized
    header = struct.pack(
        '<4s4I64s',
        b'CREV',
        len(normalized),
        crc32(payload),
        revision.modified,
        len(names),
        label,
    )
    return header + struct.pack('<I', crc32(header)) + payload + b'DONE'


def import_revision(
    client: transport.SetupClient, name: str, revision: Revision, data: bytes
) -> bytes:
    record = pack_revision(name, revision, data)
    encoded = data_files.validate_name(name).encode('ascii').hex()
    response = client.request(
        f'CNTRL HISTORY IMPORT {encoded} {revision.number} '
        f'{len(record)} {crc32(record):08x}'
    )
    if response != 'CNTRL SEND':
        raise ValueError(f'unexpected history import response: {response}')
    for offset in range(0, len(record), client.connection.usb_block_size):
        client.send_data(
            Kind.history, record[offset : offset + client.connection.usb_block_size]
        )
    if (response := client.request('CNTRL HISTORY END')) not in (
        'CNTRL OK',
        'CNTRL EXISTS',
    ):
        raise ValueError(f'unexpected history import response: {response}')
    body = record[88 + struct.unpack_from('<I', record, 16)[0] * 64 : -4]
    if client.download(name, revision.number) != body:
        raise ValueError(f'{name}/{revision.number}: restored history differs')
    return body


def reserved_numbers(directory: Path) -> list[tuple[str, int]]:
    """Recover empty and populated revision directories recorded by the card scan."""
    path = directory / 'storage.toml'
    if not path.exists():
        return []
    report = tomlkit.parse(path.read_bytes().decode()).unwrap()
    status = directory / 'status.txt'
    root = (
        next(
            (
                int(v.split('=', 1)[1])
                for v in status.read_text().split()
                if v.startswith('persistent_root=')
            ),
            0,
        )
        if status.exists()
        else 0
    )
    prefix = f'/cntrl/generations/{root:08x}/' if root else ''
    result: set[tuple[str, int]] = set()
    for entry in report.get('entries', []):
        if not isinstance(entry, dict) or not entry.get('directory'):
            continue
        value = str(entry['path'])
        if root:
            if not value.startswith(prefix):
                continue
            value = '/cntrl/' + value[len(prefix) :]
        parts = value.split('/')
        if len(parts) != 5 or parts[:3] != ['', 'cntrl', 'history']:
            continue
        if len(parts[4]) != 16 or any(
            c not in '0123456789abcdefABCDEF' for c in parts[4]
        ):
            continue
        number = int(parts[4], 16)
        if 0 < number < 2**64 - 1:
            result.add((data_files.validate_name(parts[3]), number))
    return sorted(result, key=lambda r: (r[0].lower(), r[1]))


class Change(BaseModel, frozen=True):
    path: str
    before: object
    after: object


def compare(before: bytes, after: bytes) -> list[Change]:
    first = tomlkit.parse(resolved.export(before)).unwrap()
    second = tomlkit.parse(resolved.export(after)).unwrap()
    result: list[Change] = []

    def visit(path: str, old: object, new: object) -> None:
        if isinstance(old, dict) and isinstance(new, dict):
            for k in sorted(old.keys() | new.keys()):
                if k not in old or k not in new:
                    result.append(
                        Change(
                            path=f'{path}.{k}'.lstrip('.'),
                            before=old.get(k),
                            after=new.get(k),
                        )
                    )
                else:
                    visit(f'{path}.{k}', old[k], new[k])
        elif isinstance(old, list) and isinstance(new, list):
            for i in range(max(len(old), len(new))):
                if i >= len(old) or i >= len(new):
                    result.append(
                        Change(
                            path=f'{path}[{i}]'.lstrip('.'),
                            before=old[i] if i < len(old) else None,
                            after=new[i] if i < len(new) else None,
                        )
                    )
                else:
                    visit(f'{path}[{i}]', old[i], new[i])
        elif type(old) is not type(new) or old != new:
            result.append(Change(path=path.lstrip('.'), before=old, after=new))

    visit('', first, second)
    return result


def run(command: History) -> None:
    name = data_files.validate_name(command.name).encode('ascii').hex()
    action = command.action
    if command.revision < 0 or command.revision >= 2**64 - 1:
        raise ValueError('revision out of range')
    if action in ('download', 'compare', 'restore', 'delete') and not command.revision:
        raise ValueError('choose --revision')
    if action in ('delete', 'clear') and not command.yes:
        if not sys.stdin.isatty() or input(
            f'{action} history for {command.name}? [y/N] '
        ).lower() not in ('y', 'yes'):
            raise ValueError('deletion requires confirmation or --yes')
    with transport.SerialConnection(command.port) as connection:
        client = transport.SetupClient(connection)
        prefix = 'CNTRL HISTORY'
        if action == 'list':
            for revision in revisions(client, command.name):
                print(
                    f'{revision.number}  {revision.label}  '
                    f'timestamp={revision.modified}  bytes={revision.length}'
                )
        elif action == 'save':
            label = (
                data_files.validate_name(command.label).encode('ascii').hex()
                if command.label
                else '-'
            )
            client.ok(f'CNTRL TIME {int(time.time())}')
            client.ok(f'{prefix} SAVE {name} 0 {label}')
        elif action in ('download', 'compare'):
            data = client.download(command.name, command.revision)
            if action == 'compare':
                for c in compare(client.download(command.name), data):
                    print(f'{c.path}: {c.before!r} -> {c.after!r}')
            elif command.output is None:
                raise ValueError('download requires --output (exact compiled bytes)')
            else:
                with command.output.open('xb') as file:
                    file.write(data)
        else:
            operation = 'RESTORE' if action == 'restore' else 'DELETE'
            client.ok(
                f'{prefix} {operation} {name} '
                f'{0 if action == "clear" else command.revision}'
            )
