"""The cntrl command: compile shows, upload documents and query status."""

import sys
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, ConfigDict, Field

from . import compiler, models, transport


class Compile(BaseModel, frozen=True):
    """Compile a TOML/JSON show without accessing a device."""

    document: Annotated[Path, tyro.conf.Positional]
    definitions: Path
    target: Path
    output: Path
    table: Annotated[list[Path], tyro.conf.UseAppendAction] = Field(
        default_factory=list
    )

    model_config = ConfigDict(extra='forbid')


class Upload(BaseModel, frozen=True):
    """Upload a compiled show document, never firmware."""

    document: Annotated[Path, tyro.conf.Positional]
    port: Path

    model_config = ConfigDict(extra='forbid')


class Status(BaseModel, frozen=True):
    """Read the active document's version, size and checksum."""

    port: Path

    model_config = ConfigDict(extra='forbid')


def main() -> None:
    command = tyro.cli(
        Annotated[Compile, tyro.conf.subcommand(name='compile')]
        | Annotated[Upload, tyro.conf.subcommand(name='upload')]
        | Annotated[Status, tyro.conf.subcommand(name='status')]
    )
    try:
        if isinstance(command, Compile):
            document = compiler.read_document(command.document)
            target = models.Target.model_validate(
                compiler.read_document(command.target)
            )
            result = compiler.compile_show(
                document,
                compiler.read_document(command.definitions),
                [compiler.read_document(p) for p in command.table],
                target,
            )
            command.output.write_bytes(result)
            instruments, categories, patches = compiler.HEADER.unpack_from(result)[4:7]
            print(
                f'{command.output}: {len(result)} bytes; '
                f'{instruments} instruments, {categories} categories, {patches} patches'
            )
            show = models.Show.model_validate(document)
            for alias, value in show.instruments.items():
                instrument = models.Instrument.model_validate(value)
                print(f'  {alias}: channel {instrument.channel} to {instrument.output}')
        elif isinstance(command, Upload):
            data = command.document.read_bytes()
            if len(data) < compiler.HEADER.size or data[:4] != b'CSW1':
                sys.exit('Expected a compiled show document, not firmware')
            print(transport.upload_document(command.port, data))
        else:
            print(transport.device_status(command.port))
    except (OSError, ValueError) as error:
        sys.exit(str(error))
