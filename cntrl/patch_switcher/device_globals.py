"""Host commands and portable snapshots of the pedal's sparse global overlay."""

import struct
from pathlib import Path
from typing import Annotated, Literal
from zlib import crc32

import tomlkit
import tyro
from pydantic import BaseModel, ConfigDict, Field, field_validator

from . import device_settings, devices, transport


class Snapshot(BaseModel, frozen=True):
    format: Literal[1]
    values: dict[str, Annotated[int, Field(strict=True, ge=0, le=0xFFFFFFFF)]]

    @field_validator('values')
    @classmethod
    def valid_settings(cls, value: dict[str, int]) -> dict[str, int]:
        for key, raw in value.items():
            try:
                field = int(key)
            except ValueError as error:
                raise ValueError(f'{key}: invalid device-global field id') from error
            if str(field) != key:
                raise ValueError(f'{key}: noncanonical device-global field id')
            device_settings.decode_value(field, raw)
        return value

    model_config = ConfigDict(extra='forbid')


class List(BaseModel, frozen=True):
    port: Path | None = None


class Set(BaseModel, frozen=True):
    setting: Annotated[str, tyro.conf.Positional]
    value: Annotated[str, tyro.conf.Positional]
    port: Path | None = None


class Clear(BaseModel, frozen=True):
    setting: Annotated[str, tyro.conf.Positional]
    port: Path | None = None


class Restore(BaseModel, frozen=True):
    file: Annotated[Path, tyro.conf.Positional]
    port: Path | None = None


class GlobalCLI(BaseModel, frozen=True):
    """Read or change pedal-wide preferences without opening a settings page."""

    command: (
        Annotated[List, tyro.conf.subcommand(name='list')]
        | Annotated[Set, tyro.conf.subcommand(name='set')]
        | Annotated[Clear, tyro.conf.subcommand(name='clear')]
        | Annotated[Restore, tyro.conf.subcommand(name='restore')]
    )


def dump(values: dict[int, int]) -> bytes:
    snapshot = Snapshot(format=1, values={str(k): v for k, v in sorted(values.items())})
    return tomlkit.dumps(snapshot.model_dump()).encode()


def load(data: bytes) -> dict[int, int]:
    snapshot = Snapshot.model_validate(tomlkit.parse(data.decode()).unwrap())
    return {int(k): v for k, v in snapshot.values.items()}


def journal(values: dict[int, int]) -> bytes:
    """Bootstrap an empty emulator store from a portable overlay snapshot."""
    snapshot = Snapshot(format=1, values={str(k): v for k, v in values.items()})
    known = sum(1 << int(k) for k in snapshot.values)
    body = struct.pack('<4sIIQ', b'CGLO', 1, 1, known)
    body += struct.pack(
        '<' + 'I' * len(device_settings.SETTINGS),
        *(values.get(i, 0) for i in range(len(device_settings.SETTINGS))),
    )
    return body + struct.pack('<I4s', crc32(body), b'DONE')


def supported(status: str) -> bool:
    return 'device_globals=1' in status.split()


def capture(client: transport.SetupClient) -> dict[int, int] | None:
    return (
        client.global_settings() if supported(client.request('CNTRL STATUS')) else None
    )


def run(command: List | Set | Clear | Restore) -> None:
    port = devices.select_port(command.port)
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        if isinstance(command, List):
            values = client.global_settings()
            for field, setting in enumerate(device_settings.SETTINGS):
                if field not in values:
                    value = 'inherit'
                else:
                    decoded = device_settings.decode_value(field, values[field])
                    value = (
                        str(decoded).lower()
                        if isinstance(decoded, bool)
                        else str(decoded)
                    )
                print(f'{setting.name} = {value}')
        elif isinstance(command, Restore):
            client.replace_globals(load(command.file.read_bytes()))
        else:
            field = device_settings.field_id(command.setting)
            if isinstance(command, Set):
                client.set_global(
                    field, device_settings.encode_value(field, command.value)
                )
            else:
                client.clear_global(field)
