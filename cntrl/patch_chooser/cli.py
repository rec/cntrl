"""Compile data files and manage named performance setups on the pedal."""

import os
import subprocess
import sys
import time
from contextlib import nullcontext
from datetime import datetime
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel, Field

from .. import build, flash, importing, qualify, target_process, upload
from ..emulator.cli import Emulate, emulate
from ..file_lock import writer_lock
from ..maker.morningstar.mc import font_coverage
from . import (
    backups,
    binary_format,
    compiler_errors,
    compiler_inputs,
    compiler_wire,
    data_files,
    device_globals,
    devices,
    disk_speed,
    history,
    languages,
    midi_assets,
    models,
    resolved,
    setup_bundle,
    setup_create,
    setup_sync,
    setup_sync_store,
    songs,
    source_archive,
    transport,
)
from .setup_compiler import compile_inputs, document_references


class Compile(data_files.Sources, frozen=True):
    """Compile one data-file expression without accessing a device."""

    output: Path


class CompileOutputError(ValueError):
    """The compiled setup or an asset could not be published."""


class SetupCopy(data_files.Sources, frozen=True):
    """Compile, copy and verify named setups on the pedal's SD card."""

    port: Path
    overwrite: Annotated[bool, tyro.conf.arg(aliases=['-o'])] = False


class SetupSet(BaseModel, frozen=True):
    """Print the persistent selection, or select a named setup."""

    port: Path
    name: Annotated[str | None, tyro.conf.Positional] = None


class SetupList(BaseModel, frozen=True):
    """List setups and commit dates, one per line."""

    port: Path


class SetupDelete(BaseModel, frozen=True):
    """Delete named setups after confirmation."""

    names: Annotated[list[str], tyro.conf.Positional]
    port: Path
    yes: Annotated[bool, tyro.conf.arg(aliases=['-y'])] = False


class SetupRotate(BaseModel, frozen=True):
    """Select the next setup, wrapping alphabetically."""

    port: Path
    backward: Annotated[bool, tyro.conf.arg(aliases=['-b'])] = False


class SetupDownload(BaseModel, frozen=True):
    """Download editable setups and their files; omit names to download all setups."""

    names: Annotated[list[str], tyro.conf.Positional] = Field(default_factory=list)
    port: Path
    output: Annotated[Path, tyro.conf.arg(aliases=['-o'])] = Path('.')
    force: Annotated[bool, tyro.conf.arg(aliases=['-f'])] = False


class SetupCLI(BaseModel, frozen=True):
    command: (
        Annotated[setup_create.Create, tyro.conf.subcommand(name='create')]
        | Annotated[setup_sync.Sync, tyro.conf.subcommand(name='sync')]
        | Annotated[history.History, tyro.conf.subcommand(name='history')]
        | Annotated[SetupCopy, tyro.conf.subcommand(name='copy')]
        | Annotated[SetupSet, tyro.conf.subcommand(name='set')]
        | Annotated[SetupList, tyro.conf.subcommand(name='list')]
        | Annotated[SetupDelete, tyro.conf.subcommand(name='del')]
        | Annotated[SetupRotate, tyro.conf.subcommand(name='rotate')]
        | Annotated[SetupDownload, tyro.conf.subcommand(name='download')]
        | Annotated[backups.Backup, tyro.conf.subcommand(name='backup')]
        | Annotated[backups.Restore, tyro.conf.subcommand(name='restore')]
        | Annotated[backups.Verify, tyro.conf.subcommand(name='verify')]
        | Annotated[backups.Convert, tyro.conf.subcommand(name='convert')]
        | Annotated[backups.Recover, tyro.conf.subcommand(name='recover')]
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


def run_setup(
    command: SetupCopy
    | SetupSet
    | SetupList
    | SetupDelete
    | SetupRotate
    | SetupDownload
    | history.History
    | backups.Backup
    | backups.Restore
    | backups.Verify
    | backups.Convert
    | backups.Recover
    | setup_sync.Sync
    | setup_create.Create,
    *,
    connection: transport.SerialConnection | None = None,
) -> None:
    if connection is not None and not isinstance(command, SetupCopy):
        raise ValueError('a supplied setup connection is supported for setup copy only')
    if isinstance(command, setup_create.Create):
        setup_create.run(command)
        return
    if isinstance(command, setup_sync.Sync):
        setup_sync.run(command)
        return
    if isinstance(command, backups.Backup):
        backups.backup_pedal(command.directory, command.port)
        return
    if isinstance(command, backups.Convert):
        backups.prepare_restore(
            command.directory,
            command.destination,
            models.Target.model_validate(compiler_inputs.read_document(command.target)),
        )
        print(f'Converted backup: {command.destination}')
        return
    if isinstance(command, backups.Recover):
        if not 0 <= command.root <= 0xFFFFFFFF:
            raise ValueError('root must be between 0 and 4294967295')
        with transport.SerialConnection(command.port) as connection:
            client = transport.SetupClient(connection)
            client.ok(f'CNTRL DEPLOY RECOVER {command.root:08x}')
            print(client.request('CNTRL STATUS'))
        return
    if isinstance(command, backups.Restore):
        backups.restore_setups(
            command.directory,
            command.port,
            models.Target.model_validate(compiler_inputs.read_document(command.target)),
            preserve_memory_estimates=command.preserve_memory_estimates,
        )
        return
    if isinstance(command, backups.Verify):
        backups.verify_after_flashing(
            command.directory,
            command.port,
            models.Target.model_validate(compiler_inputs.read_document(command.target)),
            preserve_memory_estimates=command.preserve_memory_estimates,
        )
        return
    if isinstance(command, history.History):
        history.run(command)
        return
    lock = (
        writer_lock(setup_sync.store(command), operation='setup sync operation')
        if isinstance(command, SetupCopy)
        else nullcontext()
    )
    with lock:
        setup_transfer(command, connection)


def setup_transfer(
    command: SetupSet
    | SetupRotate
    | SetupList
    | SetupCopy
    | SetupDownload
    | SetupDelete,
    connection: transport.SerialConnection | None,
) -> None:
    assets: dict[str, dict[int, bytes]] = {}
    movies: dict[int, bytes] = {}
    trace = data_files.Trace()
    bundle = (
        compile_inputs(command, assets, movies, trace=trace)
        if isinstance(command, SetupCopy)
        else None
    )
    midi = midi_assets.collect(bundle.setups, trace.files) if bundle is not None else {}
    with (
        nullcontext(connection)
        if connection is not None
        else transport.SerialConnection(command.port) as connection
    ):
        client = transport.SetupClient(connection)
        if isinstance(command, SetupSet):
            if command.name is not None:
                client.select(command.name)
            else:
                print(client.current())
        elif isinstance(command, SetupRotate):
            client.ok(
                'CNTRL SETUP BACKWARD' if command.backward else 'CNTRL SETUP FORWARD'
            )
        elif isinstance(command, SetupList):
            for e in client.catalogue():
                stamp = (
                    datetime.fromtimestamp(e.modified).strftime('%Y-%m-%d %H:%M:%S')
                    if e.modified
                    else 'unknown'
                )
                print(f'{e.name}  {stamp}' + ('' if e.valid else ' (invalid setup)'))
        elif isinstance(command, SetupCopy):
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
                raise compiler_errors.CompilationError(errors)
            uploaded_names = bundle.names
            bundle, upload_snapshot = setup_sync.prepare_upload(
                command, bundle, trace, client
            )
            archive = source_archive.create(
                command, trace, {n: bundle.setups[n] for n in uploaded_names}
            )
            bundle = setup_bundle.SetupBundle(
                setups={
                    n: source_archive.attach(d, archive) if n in uploaded_names else d
                    for n, d in bundle.setups.items()
                },
                page_ram2=bundle.page_ram2,
                memory=bundle.memory,
                memory_margin=bundle.memory_margin,
                metadata=bundle.metadata,
            )
            client.ok(f'CNTRL TIME {int(time.time())}')
            failures: list[str] = []
            try:
                source_archive.upload(client, archive)
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
                raise compiler_errors.CompilationError(failures)
            setup_sync.capture_assets(
                client, {n: bundle.setups[n] for n in uploaded_names}, upload_snapshot
            )
            live_globals = device_globals.capture(client)
            if live_globals is not None:
                (upload_snapshot / 'device-globals.toml').write_bytes(
                    device_globals.dump(live_globals)
                )
            setup_sync_store.advance(
                setup_sync.store(command), upload_snapshot, uploaded_names
            )
        else:
            entries = {e.name.lower(): e.name for e in client.catalogue()}
            errors: list[str] = []
            names: list[str] = []
            requested = command.names
            if isinstance(command, SetupDownload) and not requested:
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
                raise compiler_errors.CompilationError(errors)
            if isinstance(command, SetupDelete):
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
    live_globals = device_globals.capture(client)
    if live_globals is not None:
        files[Path('device-globals.toml')] = device_globals.dump(live_globals)
        if live_globals.language:
            try:
                files[Path('languages') / f'{live_globals.language}.bin'] = (
                    client.download_language(live_globals.language)
                )
            except FileNotFoundError:
                pass  # Keep the saved preference; the device uses English recovery UI.
    files.update(source_archive.download(client, setups, live_globals=live_globals))
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
        raise compiler_errors.CompilationError(errors)
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
            raise CompileOutputError(f'{destination}: asset checksum collision')
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
        data = binary_format.core(data)
        ni, nc, np = compiler_wire.HEADER.unpack_from(data)[4:7]
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
    data = bundle.pack()
    try:
        for fonts in assets.values():
            for key, font in fonts.items():
                save_asset(output.parent / 'fonts' / f'{key:08x}.cfont', font)
        for key, movie in movies.items():
            save_asset(output.parent / 'movies' / f'{key:08x}.cnsa', movie)
        output.write_bytes(data)
    except OSError as error:
        raise CompileOutputError(str(error)) from error
    return bundle


def main() -> None:
    command = tyro.cli(
        Annotated[Compile, tyro.conf.subcommand(name='compile')]
        | Annotated[SetupCLI, tyro.conf.subcommand(name='setup')]
        | Annotated[Status, tyro.conf.subcommand(name='status')]
        | Annotated[device_globals.GlobalCLI, tyro.conf.subcommand(name='global')]
        | Annotated[languages.LanguageCLI, tyro.conf.subcommand(name='language')]
        | Annotated[Songs, tyro.conf.subcommand(name='songs')]
        | Annotated[build.Build, tyro.conf.subcommand(name='build')]
        | Annotated[font_coverage.Report, tyro.conf.subcommand(name='font-coverage')]
        | Annotated[flash.Flash, tyro.conf.subcommand(name='flash')]
        | Annotated[upload.Helper, tyro.conf.subcommand(name='upload-helper')]
        | Annotated[qualify.Qualify, tyro.conf.subcommand(name='qualify')]
        | Annotated[Emulate, tyro.conf.subcommand(name='emulate')]
        | Annotated[importing.Import, tyro.conf.subcommand(name='import')],
        config=(tyro.conf.OmitSubcommandPrefixes,),
        args=sys.argv[1:] or ['setup', 'create'],
    )
    try:
        if isinstance(command, importing.Import):
            importing.run(command)
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
        elif isinstance(command, font_coverage.Report):
            command.output.write_text(font_coverage.generate())
        elif isinstance(command, Compile):
            report_bundle(compile_to_file(command, command.output))
        elif isinstance(command, Songs):
            songs.run(command.command)
        elif isinstance(command, SetupCLI):
            run_setup(command.command)
        elif isinstance(command, device_globals.GlobalCLI):
            device_globals.run(command.command)
        elif isinstance(command, languages.LanguageCLI):
            languages.run(command.command)
        elif command.command is not None:
            if command.reset_starts:
                sys.exit('--reset-starts cannot be combined with disk-speed')
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
    except EOFError:
        sys.exit('Input ended; setup creation cancelled.')
    except (
        compiler_errors.CompilationError,
        CompileOutputError,
        devices.DeviceSelectionError,
        source_archive.RecoveredSourceError,
        subprocess.CalledProcessError,
        target_process.TargetAdapterError,
        transport.UsbCommandTimeout,
        transport.UsbProtocolError,
        transport.UsbReplyError,
    ) as error:
        sys.exit(str(error))
    except (OSError, ValueError) as error:
        if isinstance(
            command,
            (
                Compile,
                Status,
                build.Build,
                flash.Flash,
                importing.Import,
                qualify.Qualify,
                upload.Helper,
            ),
        ):
            raise
        sys.exit(str(error))


if __name__ == '__main__':
    from .. import entrypoint

    entrypoint.main()
