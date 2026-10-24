"""Compile data files and manage named performance setups on the pedal."""

import os
import sys
import time
from datetime import datetime
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Annotated

import tyro
from pydantic import BaseModel, ConfigDict, Field

from . import compiler, data_files, models, resolved, transport


class Sources(BaseModel, frozen=True):
    document: Annotated[list[str], tyro.conf.Positional]
    target: list[str]
    definitions: list[str] = Field(default_factory=list)
    table: Annotated[list[list[str]], tyro.conf.UseAppendAction] = Field(
        default_factory=list
    )
    avoid_confict: bool = True

    model_config = ConfigDict(extra='forbid')


class Compile(Sources, frozen=True):
    """Compile one data-file expression without accessing a device."""

    output: Path


class Copy(Sources, frozen=True):
    """Compile, copy and verify named setups on the pedal's SD card."""

    port: Path
    overwrite: Annotated[bool, tyro.conf.arg(aliases=['-o'])] = False


class Set(BaseModel, frozen=True):
    """Print the persistent selection, or select a named setup."""

    port: Path
    name: Annotated[str | None, tyro.conf.Positional] = None


class List(BaseModel, frozen=True):
    """List setups and commit dates, one per line."""

    port: Path


class Delete(BaseModel, frozen=True):
    """Delete named setups after confirmation."""

    names: Annotated[list[str], tyro.conf.Positional]
    port: Path
    yes: Annotated[bool, tyro.conf.arg(aliases=['-y'])] = False


class Rotate(BaseModel, frozen=True):
    """Select the next setup, wrapping alphabetically."""

    port: Path
    backward: Annotated[bool, tyro.conf.arg(aliases=['-b'])] = False


class Download(BaseModel, frozen=True):
    """Download self-contained, editable TOML setups."""

    names: Annotated[list[str], tyro.conf.Positional]
    port: Path
    directory: Path = Path('.')
    overwrite: Annotated[bool, tyro.conf.arg(aliases=['-o'])] = False


class Setup(BaseModel, frozen=True):
    command: (
        Annotated[Copy, tyro.conf.subcommand(name='copy')]
        | Annotated[Set, tyro.conf.subcommand(name='set')]
        | Annotated[List, tyro.conf.subcommand(name='list')]
        | Annotated[Delete, tyro.conf.subcommand(name='del')]
        | Annotated[Rotate, tyro.conf.subcommand(name='rotate')]
        | Annotated[Download, tyro.conf.subcommand(name='download')]
    )


class Status(BaseModel, frozen=True):
    port: Path


def compile_inputs(command: Sources) -> dict[str, bytes]:
    errors: list[str] = []

    documents = data_files.expressions(command.document, errors)
    targets = data_files.expressions(command.target, errors)
    definitions = data_files.expressions(command.definitions, errors)
    tables = [e for t in command.table for e in data_files.expressions(t, errors)]
    target_data = [data_files.load(e, command.avoid_confict, errors) for e in targets]
    definition_data = [
        data_files.load(e, command.avoid_confict, errors) for e in definitions
    ]
    table_data: list[object] = [
        data_files.load(e, command.avoid_confict, errors) for e in tables
    ]
    if len(target_data) != 1:
        errors.append('expected one target expression; use + to combine target files')
    if len(definition_data) > 1:
        errors.append(
            'expected one definitions expression; use + to combine definition files'
        )
    target: models.Target | None = None
    if len(target_data) == 1:
        try:
            target = models.Target.model_validate(target_data[0])
        except ValueError as error:
            errors.append(f'target: {error}')
    if not documents:
        errors.append('at least one setup data-file expression is required')
    result: dict[str, bytes] = {}
    seen: set[str] = set()
    for e in documents:
        document = data_files.load(e, command.avoid_confict, errors)
        try:
            data_files.validate_name(e.name)
        except ValueError as error:
            errors.append(str(error))
        if e.name.lower() in seen:
            errors.append(f'{e.name}: duplicate destination name')
        seen.add(e.name.lower())
        if target is not None:
            try:
                result[e.name] = compiler.compile_show(
                    document,
                    definition_data[0] if definition_data else {'setups': []},
                    table_data,
                    target,
                )
            except compiler.CompilationError as error:
                errors.extend(f'{e.name}: {x}' for x in error.errors)
    if errors:
        raise compiler.CompilationError(errors)
    return result


def run_setup(command: Copy | Set | List | Delete | Rotate | Download) -> None:
    compiled = compile_inputs(command) if isinstance(command, Copy) else {}
    with transport.SerialConnection(command.port) as connection:
        client = transport.SetupClient(connection)
        if isinstance(command, Set):
            if command.name is not None:
                client.select(command.name)
            else:
                print(client.current())
        elif isinstance(command, Rotate):
            client.ok(
                'CNTRL SETUP BACKWARD' if command.backward else 'CNTRL SETUP FORWARD'
            )
        elif isinstance(command, List):
            for e in client.catalogue():
                stamp = (
                    datetime.fromtimestamp(e.modified).strftime('%Y-%m-%d %H:%M:%S')
                    if e.modified
                    else 'unknown'
                )
                print(f'{e.name}  {stamp}' + ('' if e.valid else ' (invalid setup)'))
        elif isinstance(command, Copy):
            existing = {e.name.lower(): e.name for e in client.catalogue()}
            errors = [
                f'{n}: setup exists; use --overwrite/-o'
                for n in compiled
                if n.lower() in existing and not command.overwrite
            ]
            errors += [
                f'{n}: differs only in case from {existing[n.lower()]}'
                for n in compiled
                if n.lower() in existing and existing[n.lower()] != n
            ]
            if errors:
                raise compiler.CompilationError(errors)
            client.ok(f'CNTRL TIME {int(time.time())}')
            failures: list[str] = []
            try:
                for name, data in compiled.items():
                    client.copy(name, data, command.overwrite)
                    print(
                        f'{name}: copied and verified ({len(data)} bytes)', flush=True
                    )
            except (OSError, ValueError) as error:
                failures.append(str(error))
            try:
                client.ok('CNTRL SETUP FINISH')
            except (OSError, ValueError) as error:
                failures.append(f'batch completion: {error}')
            if failures:
                raise compiler.CompilationError(failures)
        else:
            entries = {e.name.lower(): e.name for e in client.catalogue()}
            errors: list[str] = []
            names: list[str] = []
            for n in command.names:
                try:
                    data_files.validate_name(n)
                except ValueError as error:
                    errors.append(str(error))
                if n.lower() not in entries:
                    errors.append(f'{n}: no such setup')
                elif entries[n.lower()] in names:
                    errors.append(f'{n}: duplicate requested setup')
                else:
                    names.append(entries[n.lower()])
            if not names:
                errors.append('at least one setup name is required')
            if isinstance(command, Download):
                if not command.directory.is_dir():
                    errors.append(f'{command.directory}: not a directory')
                for n in names:
                    if (
                        command.directory / f'{n}.toml'
                    ).exists() and not command.overwrite:
                        errors.append(f'{n}.toml: exists; use --overwrite/-o')
            if errors:
                raise compiler.CompilationError(errors)
            if isinstance(command, Delete):
                if not command.yes:
                    if not sys.stdin.isatty():
                        raise ValueError(
                            'deletion requires an interactive confirmation or --yes/-y'
                        )
                    print('Delete: ' + ', '.join(names))
                    if input('Are you sure? [y/N] ').strip().lower() not in (
                        'y',
                        'yes',
                    ):
                        return
                try:
                    client.delete(names)
                except (OSError, ValueError):
                    try:
                        remaining = {e.name.lower() for e in client.catalogue()}
                        for n in names:
                            if n.lower() not in remaining:
                                print(
                                    f'{n}: deleted before batch failure',
                                    file=sys.stderr,
                                )
                    except (OSError, ValueError) as error:
                        print(
                            f'Unable to verify partial deletion results: {error}',
                            file=sys.stderr,
                        )
                    raise
                print('Deleted: ' + ', '.join(names))
            else:
                for n in names:
                    text = resolved.export(client.download(n))
                    destination = command.directory / f'{n}.toml'
                    # Complete transfer before touching the destination; publish
                    # a synced sibling atomically, including overwrite protection.
                    with NamedTemporaryFile(
                        mode='w', dir=command.directory, prefix='.cntrl-', delete=False
                    ) as output:
                        temporary = Path(output.name)
                        output.write(text)
                        output.flush()
                        os.fsync(output.fileno())
                    try:
                        if command.overwrite:
                            temporary.replace(destination)
                        else:
                            destination.hardlink_to(temporary)
                    finally:
                        temporary.unlink(missing_ok=True)
                    print(destination)


def main() -> None:
    command = tyro.cli(
        Annotated[Compile, tyro.conf.subcommand(name='compile')]
        | Annotated[Setup, tyro.conf.subcommand(name='setup')]
        | Annotated[Status, tyro.conf.subcommand(name='status')],
        config=(tyro.conf.OmitSubcommandPrefixes,),
    )
    try:
        if isinstance(command, Compile):
            results = compile_inputs(command)
            if len(results) != 1:
                raise ValueError(
                    'compile accepts one expression; use setup copy for multiple setups'
                )
            data = next(iter(results.values()))
            command.output.write_bytes(data)
            ni, nc, np = compiler.HEADER.unpack_from(data)[4:7]
            print(
                f'{command.output}: {len(data)} bytes; '
                f'{ni} instruments, {nc} categories, {np} patches'
            )
            # Report the actual compiled routes, including composition overrides.
            for i in range(ni):
                offset = 76 + i * 68
                alias = data[offset : offset + 32].split(b'\0')[0].decode('ascii')
                route = data[offset + 32 : offset + 64].split(b'\0')[0].decode('ascii')
                print(f'  {alias}: channel {data[offset + 64] + 1} to {route}')
        elif isinstance(command, Setup):
            run_setup(command.command)
        else:
            print(transport.device_status(command.port))
    except (OSError, ValueError) as error:
        sys.exit(str(error))
