"""Host commands and portable snapshots of the pedal's sparse global overlay."""

import struct
from pathlib import Path
from typing import Annotated, Literal
from zlib import crc32

import tomlkit
import tyro
from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    ValidationError,
    field_validator,
    model_validator,
)
from tomlkit.exceptions import ParseError

from . import device_settings, devices, global_overlay, transport


class GlobalRestoreError(ValueError):
    """The selected device-global snapshot could not be loaded."""


class Snapshot(BaseModel, frozen=True):
    format: Literal[1, 2, 3]
    startup_set: (
        Annotated[str, Field(min_length=1, max_length=63, pattern=r'^[ -~]+$')] | None
    ) = None
    values: dict[str, Annotated[int, Field(strict=True, ge=0, le=0xFFFFFFFF)]] = Field(
        default_factory=dict
    )
    language: (
        Annotated[
            str,
            Field(
                min_length=2,
                max_length=15,
                pattern=r'^[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$',
            ),
        ]
        | None
    ) = None

    @model_validator(mode='after')
    def versioned_language(self) -> 'Snapshot':
        if self.format == 1 and self.language is not None:
            raise ValueError('version-1 globals cannot contain a language')
        if self.format < 3 and self.startup_set is not None:
            raise ValueError('startup sets require version-3 globals')
        return self

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


def dump(overlay: global_overlay.Overlay) -> bytes:
    snapshot = Snapshot(
        format=3,
        startup_set=overlay.startup_set,
        values={str(k): v for k, v in sorted(overlay.values.items())},
        language=overlay.language,
    )
    return tomlkit.dumps(snapshot.model_dump(exclude_none=True)).encode()


def load(data: bytes) -> global_overlay.Overlay:
    snapshot = Snapshot.model_validate(tomlkit.parse(data.decode()).unwrap())
    return global_overlay.Overlay(
        values={int(k): v for k, v in snapshot.values.items()},
        language=snapshot.language,
        startup_set=snapshot.startup_set,
    )


def journal(overlay: global_overlay.Overlay) -> bytes:
    """Bootstrap an empty emulator store from a portable overlay snapshot."""
    known = sum(1 << i for i in overlay.values)
    body = struct.pack('<4sIIQ', b'CGLO', 3, 1, known)
    body += struct.pack(
        '<' + 'I' * len(device_settings.SETTINGS),
        *(overlay.values.get(i, 0) for i in range(len(device_settings.SETTINGS))),
    )
    body += (overlay.language or '').encode().ljust(16, b'\0')
    body += (overlay.startup_set or '').encode().ljust(64, b'\0')
    return body + struct.pack('<I4s', crc32(body), b'DONE')


def supported(status: str) -> bool:
    return 'device_globals=1' in status.split()


def capture(client: transport.SetupClient) -> global_overlay.Overlay | None:
    return (
        client.global_settings() if supported(client.request('CNTRL STATUS')) else None
    )


def run(command: List | Set | Clear | Restore) -> None:
    restore = None
    if isinstance(command, Restore):
        try:
            restore = load(command.file.read_bytes())
        except (OSError, UnicodeDecodeError, ParseError, ValidationError) as error:
            raise GlobalRestoreError(f'{command.file}: {error}') from error
    port = devices.select_port(command.port)
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        if isinstance(command, List):
            values = client.global_settings()
            print(f'language = {values.language or "en (default)"}')
            for field, setting in enumerate(device_settings.SETTINGS):
                if setting is None:
                    continue
                if field not in values.values:
                    value = 'inherit'
                else:
                    decoded = device_settings.decode_value(field, values.values[field])
                    value = (
                        str(decoded).lower()
                        if isinstance(decoded, bool)
                        else str(decoded)
                    )
                print(f'{setting.name} = {value}')
        elif isinstance(command, Restore):
            assert restore is not None
            client.replace_globals(restore)
        elif command.setting == 'language':
            if isinstance(command, Set):
                client.set_language(command.value)
            else:
                client.clear_language()
        else:
            field = device_settings.field_id(command.setting)
            if isinstance(command, Set):
                try:
                    encoded = device_settings.encode_value(field, command.value)
                except ValidationError as error:
                    raise device_settings.DeviceSettingError(str(error)) from error
                client.set_global(field, encoded)
            else:
                client.clear_global(field)
