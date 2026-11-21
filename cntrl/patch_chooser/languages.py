"""Install prebuilt language packs without a compiler or font tools."""

from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, Field

from ..localization import build, catalogue
from . import devices, transport


class Install(BaseModel, frozen=True):
    language: Annotated[
        str,
        Field(max_length=15, pattern=r'^[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$'),
        tyro.conf.Positional,
    ]
    port: Path | None = None


class LanguageCLI(BaseModel, frozen=True):
    command: (
        Annotated[Install, tyro.conf.subcommand(name='install')]
        | Annotated[build.Build, tyro.conf.subcommand(name='build')]
    )


def pack(language: str) -> bytes:
    path = Path(catalogue.__file__).parent / 'packs' / f'{language}.bin'
    try:
        data = path.read_bytes()
    except OSError as error:
        raise catalogue.CatalogueError(str(error)) from error
    if catalogue.inspect(data).language != language:
        raise catalogue.CatalogueError(f'{path}: language pack identity mismatch')
    return data


def run(command: Install | build.Build) -> None:
    if isinstance(command, build.Build):
        build.build(command.output)
        catalogue.generate(command.header)
        return
    packs = {tag: pack(tag) for tag in dict.fromkeys(['en', command.language])}
    with transport.SerialConnection(devices.select_port(command.port)) as connection:
        client = transport.SetupClient(connection)
        # Install the matching English catalogue alongside the selected locale.
        for tag, data in packs.items():
            client.copy_language(tag, data)
        selected = client.global_settings().language
        if selected is None:
            client.clear_language()
        else:
            client.set_language(selected)
