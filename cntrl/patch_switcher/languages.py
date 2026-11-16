"""Install prebuilt language packs without a compiler or font tools."""

from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, Field

from ..localization import catalogue
from . import devices, transport


class Install(BaseModel, frozen=True):
    language: Annotated[
        str,
        Field(max_length=15, pattern=r'^[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$'),
        tyro.conf.Positional,
    ]
    port: Path | None = None


class LanguageCLI(BaseModel, frozen=True):
    command: Annotated[Install, tyro.conf.subcommand(name='install')]


def pack(language: str) -> bytes:
    data = (Path(catalogue.__file__).parent / 'packs' / f'{language}.bin').read_bytes()
    if catalogue.inspect(data).language != language:
        raise ValueError('language pack identity mismatch')
    return data


def run(command: Install) -> None:
    with transport.SerialConnection(devices.select_port(command.port)) as connection:
        client = transport.SetupClient(connection)
        # Include English so the picker can show native names before switching.
        for tag in dict.fromkeys(['en', command.language]):
            client.copy_language(tag, pack(tag))
        selected = client.global_settings().language
        if selected is None:
            client.clear_language()
        else:
            client.set_language(selected)
