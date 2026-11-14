"""Compile data files and manage named performance setups on the pedal."""

import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel, Field

from .. import build, flash, qualify, upload
from ..emulator.cli import Emulate, emulate
from ..maker.morningstar.mc import import_backup
from . import (
    backups,
    compiler,
    data_files,
    devices,
    disk_speed,
    history,
    midi_assets,
    models,
    resolved,
    setup_bundle,
    setup_sync,
    songs,
    transport,
)


class Compile(data_files.Sources, frozen=True):
    """Compile one data-file expression without accessing a device."""

    output: Path


class Copy(data_files.Sources, frozen=True):
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
    """Download editable setups and their files; omit names to download all setups."""

    names: Annotated[list[str], tyro.conf.Positional] = Field(default_factory=list)
    port: Path
    output: Annotated[Path, tyro.conf.arg(aliases=['-o'])] = Path('.')
    force: Annotated[bool, tyro.conf.arg(aliases=['-f'])] = False


class SetupCLI(BaseModel, frozen=True):
    command: (
        Annotated[setup_sync.Sync, tyro.conf.subcommand(name='sync')]
        | Annotated[history.History, tyro.conf.subcommand(name='history')]
        | Annotated[Copy, tyro.conf.subcommand(name='copy')]
        | Annotated[Set, tyro.conf.subcommand(name='set')]
        | Annotated[List, tyro.conf.subcommand(name='list')]
        | Annotated[Delete, tyro.conf.subcommand(name='del')]
        | Annotated[Rotate, tyro.conf.subcommand(name='rotate')]
        | Annotated[Download, tyro.conf.subcommand(name='download')]
        | Annotated[backups.Backup, tyro.conf.subcommand(name='backup')]
        | Annotated[backups.Restore, tyro.conf.subcommand(name='restore')]
        | Annotated[backups.Verify, tyro.conf.subcommand(name='verify')]
    )


class Songs(BaseModel, frozen=True):
    command: (
        Annotated[songs.Copy, tyro.conf.subcommand(name='copy')]
        | Annotated[songs.Download, tyro.conf.subcommand(name='download')]
    )


class DiskSpeed(BaseModel, frozen=True):
    """Measure USB disk throughput in bytes/second with live byte progress.

    Upload, sync, download and verify a 1,000,000-byte scratch file. Report
    upload, download and round-trip throughput, then remove the scratch file.
    """


@tyro.conf.configure(tyro.conf.CascadeSubcommandArgs)
class Status(BaseModel, frozen=True):
    """Show device status or measure USB disk throughput."""

    port: Path | None = None
    """Select automatically unless a serial endpoint is explicitly supplied."""
    reset_starts: bool = False
    """Reset the saved startup counter to 1 before reporting status."""
    command: Annotated[
        Annotated[DiskSpeed, tyro.conf.subcommand(name='disk-speed', prefix_name=False)]
        | None,
        tyro.conf.DisallowNone,
    ] = None


def compile_inputs(
    command: data_files.Sources,
    assets: dict[str, dict[int, bytes]] | None = None,
    movies: dict[int, bytes] | None = None,
    *,
    trace: data_files.Trace | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> setup_bundle.SetupBundle:
    errors: list[str] = []

    documents = data_files.expressions(command.document, errors)
    targets = data_files.expressions(command.target, errors)
    definitions = data_files.expressions(command.definitions, errors)
    tables = [e for t in command.table for e in data_files.expressions(t, errors)]
    target_data = [
        data_files.load_target(
            e, command.avoid_confict, errors, trace=trace, overrides=overrides
        )
        for e in targets
    ]
    definition_data = [
        data_files.load(
            e, command.avoid_confict, errors, trace=trace, overrides=overrides
        )
        for e in definitions
    ]
    table_data: list[object] = [
        data_files.load(
            e, command.avoid_confict, errors, trace=trace, overrides=overrides
        )
        for e in tables
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
    sources: dict[str, object] = {}
    # Companion documents use the same logical names on the host and SD card.
    # Load the whole dependency graph before compiling or opening a transport.
    for e in documents:
        sources[e.name] = data_files.load(
            e, command.avoid_confict, errors, trace=trace, overrides=overrides
        )
    index = 0
    while index < len(documents):
        expression = documents[index]
        index += 1
        for name in document_references(sources[expression.name]):
            if any(n.lower() == name.lower() for n in sources):
                continue
            try:
                data_files.validate_name(name)
            except ValueError as error:
                errors.append(f'{expression.paths[0]}: {error}')
                continue
            companion = data_files.DataFile(
                name=name, paths=[expression.paths[0].parent / f'{name}.toml']
            )
            sources[name] = data_files.load(
                companion,
                command.avoid_confict,
                errors,
                trace=trace,
                overrides=overrides,
            )
            documents.append(companion)
    for e in documents:
        document = sources[e.name]
        try:
            data_files.validate_name(e.name)
        except ValueError as error:
            errors.append(str(error))
        if e.name.lower() in seen:
            errors.append(f'{e.name}: duplicate destination name')
        seen.add(e.name.lower())
        if target is not None:
            try:
                fonts: dict[int, bytes] = {}
                result[e.name] = compiler.compile_show(
                    document,
                    definition_data[0] if definition_data else {'profiles': []},
                    table_data,
                    target,
                    fonts,
                    sources,
                    movies,
                )
                if assets is not None:
                    assets[e.name] = fonts
            except compiler.CompilationError as error:
                errors.extend(f'{e.paths[0]} ({e.name}): {x}' for x in error.errors)
    if trace is not None:
        global_names = {e.name for e in [*targets, *definitions, *tables]}
        for consumers in trace.consumers.values():
            if any(n in global_names for n in consumers):
                consumers[:] = list(
                    dict.fromkeys(
                        [n for n in consumers if n not in global_names]
                        + [e.name for e in documents]
                    )
                )
    if errors:
        raise compiler.CompilationError(errors)
    try:
        assert target is not None
        return setup_bundle.build(result, sources, target.memory, command.memory_margin)
    except ValueError as error:
        raise compiler.CompilationError([str(error)]) from error


def document_references(document: object) -> list[str]:
    result: list[str] = []
    pending = [document]
    while pending:
        value = pending.pop()
        if isinstance(value, dict):
            if 'patch' in value and isinstance(name := value.get('document'), str):
                if name not in result:
                    result.append(name)
            pending.extend(value.values())
        elif isinstance(value, list):
            pending.extend(value)
    return result


def run_setup(
    command: Copy
    | Set
    | List
    | Delete
    | Rotate
    | Download
    | history.History
    | backups.Backup
    | backups.Restore
    | backups.Verify
    | setup_sync.Sync,
) -> None:
    if isinstance(command, setup_sync.Sync):
        setup_sync.run(command)
        return
    if isinstance(command, backups.Backup):
        backups.backup_pedal(command.directory, command.port)
        return
    if isinstance(command, backups.Restore):
        backups.restore_setups(
            command.directory,
            command.port,
            models.Target.model_validate(compiler.read_document(command.target)),
            preserve_memory_estimates=command.preserve_memory_estimates,
        )
        return
    if isinstance(command, backups.Verify):
        backups.verify_after_flashing(
            command.directory,
            command.port,
            models.Target.model_validate(compiler.read_document(command.target)),
            preserve_memory_estimates=command.preserve_memory_estimates,
        )
        return
    if isinstance(command, history.History):
        history.run(command)
        return
    assets: dict[str, dict[int, bytes]] = {}
    movies: dict[int, bytes] = {}
    trace = data_files.Trace()
    bundle = (
        compile_inputs(command, assets, movies, trace=trace)
        if isinstance(command, Copy)
        else None
    )
    midi = midi_assets.collect(bundle.setups, trace.files) if bundle is not None else {}
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
            assert bundle is not None
            compiled = bundle.setups
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
            uploaded_names = bundle.names
            bundle, upload_snapshot = setup_sync.prepare_upload(
                command, bundle, trace, client
            )
            client.ok(f'CNTRL TIME {int(time.time())}')
            failures: list[str] = []
            try:
                for name in bundle.names:
                    for key, font in assets.get(name, {}).items():
                        client.copy_font(key, font)
                for data in movies.values():
                    client.copy_movie(data)
                for name, data in midi.items():
                    client.copy_midi(name, data, command.overwrite)
                setup_sync.check_upload(client, upload_snapshot.parent)
                client.copy_bundle(bundle.pack(), command.overwrite or bool(existing))
                report_bundle(bundle)
            except (OSError, ValueError) as error:
                failures.append(str(error))
            try:
                client.ok('CNTRL SETUP FINISH')
            except (OSError, ValueError) as error:
                failures.append(f'batch completion: {error}')
            if failures:
                raise compiler.CompilationError(failures)
            setup_sync.capture_assets(
                client, {n: bundle.setups[n] for n in uploaded_names}, upload_snapshot
            )
            setup_sync.advance(
                setup_sync.store(command), upload_snapshot, uploaded_names
            )
        else:
            entries = {e.name.lower(): e.name for e in client.catalogue()}
            errors: list[str] = []
            names: list[str] = []
            requested = command.names
            if isinstance(command, Download) and not requested:
                requested = list(entries.values())
            for n in requested:
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
                files = download_files(client, names)
                publish_download(files, command.output, command.force)


def download_files(
    client: transport.SetupClient, names: list[str]
) -> dict[Path, bytes]:
    """Collect a complete setup/dependency batch without writing host files."""
    entries = {e.name.lower(): e.name for e in client.catalogue()}
    pending = list(names)
    setups: dict[str, bytes] = {}
    files: dict[Path, bytes] = {}
    for name in pending:
        data_files.validate_name(name)
        if name.lower() not in entries:
            raise ValueError(f'{name}: no such setup')
        name = entries[name.lower()]
        if name in setups:
            continue
        data = client.download(name)
        text = resolved.export(data)
        source = tomlkit.parse(text).unwrap()
        setups[name] = data
        files[Path(f'{name}.toml')] = text.encode()
        pending.extend(document_references(source))
    for data in setups.values():
        settings = models.Setup.model_validate(
            tomlkit.parse(resolved.export(data)).unwrap()
        )
        for value in settings.fonts.values():
            path = Path(value)
            if path not in files:
                files[path] = client.download_font(int(path.stem, 16))
        for value in settings.splash_movie + settings.idle_movie:
            path = Path(value)
            if path not in files:
                files[path] = client.download_movie(int(path.stem, 16))
    for name in midi_assets.referenced(setups):
        path = Path(name)
        data = client.download_midi(name)
        if path in files and files[path] != data:
            raise ValueError(
                f'{name}: MIDI file conflicts with another downloaded file'
            )
        files[path] = data
    return files


def publish_download(
    files: dict[Path, bytes], output: Path, force: bool = False
) -> None:
    """Preflight every destination before creating directories or writing files."""
    paths = {str(p).casefold(): p for p in files}
    errors: list[str] = []
    if len(paths) != len(files):
        errors.append('downloaded filenames differ only in case')
    for relative in files:
        if relative.is_absolute() or relative.drive or '..' in relative.parts:
            errors.append(f'{relative}: invalid downloaded asset path')
            continue
        destination = output / relative
        if destination.is_dir():
            errors.append(f'{destination}: is a directory')
        elif not force and (destination.exists() or destination.is_symlink()):
            errors.append(f'{destination}: exists; use --force/-f')
        for parent in relative.parents:
            if str(parent).casefold() in paths:
                errors.append(f'{parent}: required as both a file and a directory')
        for parent in destination.parents:
            if (parent.exists() or parent.is_symlink()) and not parent.is_dir():
                errors.append(f'{parent}: not a directory')
    if errors:
        raise compiler.CompilationError(errors)
    for relative, data in files.items():
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        stream = NamedTemporaryFile(
            dir=destination.parent, prefix='.cntrl-', delete=False
        )
        temporary = Path(stream.name)
        try:
            with stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            if force:
                temporary.replace(destination)
            else:
                destination.hardlink_to(temporary)
        finally:
            temporary.unlink(missing_ok=True)
        print(destination)


def save_asset(destination: Path, data: bytes) -> None:
    destination.parent.mkdir(exist_ok=True)
    if destination.exists():
        if destination.read_bytes() != data:
            raise ValueError(f'{destination}: asset checksum collision')
        return
    with NamedTemporaryFile(
        dir=destination.parent, prefix='.cntrl-', delete=False
    ) as output:
        temporary = Path(output.name)
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    try:
        destination.hardlink_to(temporary)
    finally:
        temporary.unlink(missing_ok=True)


def report_bundle(bundle: setup_bundle.SetupBundle) -> None:
    for i, (name, size) in enumerate(
        zip(bundle.names, bundle.setup_sizes, strict=True)
    ):
        data = bundle.setups[name]
        ni, nc, np = compiler.HEADER.unpack_from(data)[4:7]
        print(
            f'{i}: {name}: {size} RAM bytes; {ni} instruments, '
            f'{nc} patch cycles, {np} patches'
        )
        for category, needed in resolved.arm_memory(data).items():
            if needed:
                print(
                    f'  {category}: {needed} additional arm RAM bytes estimated '
                    '(USB input cable limit; retained takes and current '
                    'playback excluded)'
                )
        for j in range(ni):
            offset = 76 + j * 68
            alias = data[offset : offset + 32].split(b'\0')[0].decode('ascii')
            route = data[offset + 32 : offset + 64].split(b'\0')[0].decode('ascii')
            print(f'  {alias}: channel {data[offset + 64] + 1} to {route}')
    print(f'total_setup_ram = {bundle.total_setup_ram}')
    print(
        f'total_ram2_needs() = {bundle.total_ram2_needs()} '
        '(prefill estimate, not the linked allocation budget)'
    )
    print(f'total_free_ram2() = {bundle.total_free_ram2()}')
    print(
        'static_setups'
        if bundle.total_setup_ram <= bundle.total_free_ram2()
        else 'LRU cache'
    )


def compile_to_file(
    command: data_files.Sources, output: Path
) -> setup_bundle.SetupBundle:
    assets: dict[str, dict[int, bytes]] = {}
    movies: dict[int, bytes] = {}
    bundle = compile_inputs(command, assets, movies)
    for fonts in assets.values():
        for key, font in fonts.items():
            save_asset(output.parent / 'fonts' / f'{key:08x}.cfont', font)
    for key, movie in movies.items():
        save_asset(output.parent / 'movies' / f'{key:08x}.cnsa', movie)
    output.write_bytes(bundle.pack())
    return bundle


def main() -> None:
    command = tyro.cli(
        Annotated[Compile, tyro.conf.subcommand(name='compile')]
        | Annotated[SetupCLI, tyro.conf.subcommand(name='setup')]
        | Annotated[Status, tyro.conf.subcommand(name='status')]
        | Annotated[Songs, tyro.conf.subcommand(name='songs')]
        | Annotated[build.Build, tyro.conf.subcommand(name='build')]
        | Annotated[flash.Flash, tyro.conf.subcommand(name='flash')]
        | Annotated[upload.Helper, tyro.conf.subcommand(name='upload-helper')]
        | Annotated[qualify.Qualify, tyro.conf.subcommand(name='qualify')]
        | Annotated[Emulate, tyro.conf.subcommand(name='emulate')]
        | Annotated[
            import_backup.ImportBackup, tyro.conf.subcommand(name='import-morningstar')
        ],
        config=(tyro.conf.OmitSubcommandPrefixes,),
    )
    try:
        if isinstance(command, import_backup.ImportBackup):
            import_backup.run(command)
        elif isinstance(command, Emulate):
            emulate(command, compile_to_file)
        elif isinstance(command, qualify.Qualify):
            qualify.run(command)
        elif isinstance(command, upload.Helper):
            upload.run(command)
        elif isinstance(command, flash.Flash):
            flash.run(command)
        elif isinstance(command, build.Build):
            build.run(command)
        elif isinstance(command, Compile):
            report_bundle(compile_to_file(command, command.output))
        elif isinstance(command, Songs):
            songs.run(command.command)
        elif isinstance(command, SetupCLI):
            run_setup(command.command)
        elif command.command is not None:
            if command.reset_starts:
                raise ValueError('--reset-starts cannot be combined with disk-speed')
            rates = disk_speed.measure(devices.select_port(command.port))
            print(f'Upload: {rates.upload_bytes_per_second:,.0f} bytes/second')
            print(f'Download: {rates.download_bytes_per_second:,.0f} bytes/second')
            print(f'Round trip: {rates.round_trip_bytes_per_second:,.0f} bytes/second')
        else:
            print(
                transport.device_status(
                    devices.select_port(command.port), command.reset_starts
                )
            )
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
