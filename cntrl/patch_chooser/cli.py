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
from pydantic import BaseModel, Field, ValidationError
from tyro import _errors, _fmtlib

from .. import build, flash, importing, qualify, target_process
from ..animation.asset import AnimationFormatError
from ..emulator.cli import Emulate, emulate
from ..emulator.errors import EmulatorError, EmulatorFileError
from ..file_lock import WriterLockError, WriterLockHeld, writer_lock
from ..fonts.assets import FontDecodeError
from ..localization import catalogue
from ..maker.morningstar.mc import font_coverage
from . import (
    autocycles,
    backups,
    binary_format,
    compiler_errors,
    compiler_inputs,
    compiler_wire,
    data_files,
    device_globals,
    device_settings,
    devices,
    disk_speed,
    history,
    languages,
    layout,
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
from .setup_compiler import (
    SetupInputs,
    compile_inputs,
    compile_loaded,
    document_references,
)


class Compile(data_files.Sources, frozen=True):
    """Compile a setup and sync saved owner contact details if a pedal is connected."""

    output: Path
    port: Path | None = None


class CompileOutputError(ValueError):
    """The compiled setup or an asset could not be published."""


class DownloadOutputError(OSError):
    """A downloaded file could not be published to its host destination."""


class SetupCopy(data_files.Sources, frozen=True):
    """Compile, copy and verify named setups on the pedal's SD card."""

    port: Path
    overwrite: Annotated[bool, tyro.conf.arg(aliases=['-o'])] = False


class SetupSet(BaseModel, frozen=True):
    """Print the persistent selection, or select a named setup."""

    port: Path
    name: Annotated[str | None, tyro.conf.Positional] = None


class SetupAddDoom(BaseModel, frozen=True):
    """Insert Doom into one live setup without recompiling its patches or fonts."""

    name: Annotated[str, tyro.conf.Positional]
    port: Path
    target: Path
    region: str
    before: str
    backup: Path
    """New file for the original compiled setup."""


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
        | Annotated[autocycles.AddCycle, tyro.conf.subcommand(name='add-cycle')]
        | Annotated[setup_sync.Sync, tyro.conf.subcommand(name='sync')]
        | Annotated[history.History, tyro.conf.subcommand(name='history')]
        | Annotated[SetupCopy, tyro.conf.subcommand(name='copy')]
        | Annotated[SetupAddDoom, tyro.conf.subcommand(name='add-doom')]
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
        Annotated[songs.New, tyro.conf.subcommand(name='new')]
        | Annotated[songs.Copy, tyro.conf.subcommand(name='copy')]
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


class CommandLine(BaseModel, frozen=True):
    verbose: bool = False
    """Request verbose output."""

    command: (
        Annotated[Compile, tyro.conf.subcommand(name='compile')]
        | Annotated[SetupCLI, tyro.conf.subcommand(name='setup')]
        | Annotated[Status, tyro.conf.subcommand(name='status')]
        | Annotated[device_globals.GlobalCLI, tyro.conf.subcommand(name='global')]
        | Annotated[languages.LanguageCLI, tyro.conf.subcommand(name='language')]
        | Annotated[Songs, tyro.conf.subcommand(name='songs')]
        | Annotated[build.Build, tyro.conf.subcommand(name='build')]
        | Annotated[font_coverage.Report, tyro.conf.subcommand(name='font-coverage')]
        | Annotated[flash.Flash, tyro.conf.subcommand(name='flash')]
        | Annotated[qualify.Qualify, tyro.conf.subcommand(name='qualify')]
        | Annotated[Emulate, tyro.conf.subcommand(name='emulate')]
        | Annotated[importing.Import, tyro.conf.subcommand(name='import')]
    )


def run_setup(
    command: SetupCopy
    | SetupAddDoom
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
    | autocycles.AddCycle
    | setup_create.Create,
    *,
    connection: transport.SerialConnection | None = None,
) -> None:
    if connection is not None and not isinstance(command, SetupCopy):
        raise ValueError('a supplied setup connection is supported for setup copy only')
    if isinstance(command, setup_create.Create):
        setup_create.run(command)
        return
    if isinstance(command, autocycles.AddCycle):
        autocycles.run(command)
        return
    if isinstance(command, SetupAddDoom):
        if 'doom' not in backups.read_target(command.target).functions:
            raise compiler_errors.CompilationError(
                ['Doom is unavailable on this target']
            )
        with transport.SerialConnection(command.port) as serial:
            client = transport.SetupClient(serial)
            original = client.download(command.name)
            updated = resolved.insert_doom_page(
                original, command.region, command.before
            )
            if updated != original:
                try:
                    with command.backup.open('xb') as backup:
                        backup.write(original)
                except OSError as error:
                    raise DownloadOutputError(str(error)) from error
                client.copy(command.name, updated, overwrite=True)
            print(
                f'{command.name}: Doom before {command.before}, verified on the pedal'
            )
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
            backups.read_target(command.target),
        )
        print(f'Converted backup: {command.destination}')
        return
    if isinstance(command, backups.Recover):
        if not 0 <= command.root <= 0xFFFFFFFF:
            raise compiler_errors.CompilationError(
                ['root must be between 0 and 4294967295']
            )
        with transport.SerialConnection(command.port) as connection:
            client = transport.SetupClient(connection)
            client.ok(f'CNTRL DEPLOY RECOVER {command.root:08x}')
            print(client.request('CNTRL STATUS'))
        return
    if isinstance(command, backups.Restore):
        backups.restore_setups(
            command.directory,
            command.port,
            backups.read_target(command.target),
            preserve_memory_estimates=command.preserve_memory_estimates,
        )
        return
    if isinstance(command, backups.Verify):
        backups.verify_after_flashing(
            command.directory,
            command.port,
            backups.read_target(command.target),
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
    if isinstance(command, SetupDelete):
        if not command.names:
            raise compiler_errors.CompilationError(
                ['at least one setup name is required']
            )
        if not command.yes and not sys.stdin.isatty():
            raise compiler_errors.CompilationError(
                ['deletion requires an interactive confirmation or --yes/-y']
            )
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
                stamp = 'unknown'
                if e.modified:
                    try:
                        modified = datetime.fromtimestamp(e.modified)
                    except (OverflowError, OSError, ValueError) as error:
                        raise transport.UsbReplyError(
                            f'{e.name}: invalid commit timestamp {e.modified}'
                        ) from error
                    stamp = modified.strftime('%Y-%m-%d %H:%M:%S')
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
            unexpected: BaseException | None = None
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
            except (
                *transport.USB_ERRORS,
                transport.UsbTransferInputError,
                setup_sync_store.SyncError,
                setup_sync_store.SyncFileError,
            ) as error:
                failures.append(str(error))
            except BaseException as error:
                unexpected = error
                raise
            finally:
                # Cancellation uses the serial connection's release path.
                if unexpected is None or isinstance(unexpected, Exception):
                    try:
                        client.ok('CNTRL SETUP FINISH')
                    except transport.USB_ERRORS as error:
                        message = f'batch completion: {error}'
                        if unexpected is None:
                            failures.append(message)
                        else:
                            unexpected.add_note(message)
            if failures:
                raise compiler_errors.CompilationError(failures)
            if (contact := device_globals.saved_contact()) is not None:
                device_globals.sync_contact(contact, client=client)
            setup_sync.capture_assets(
                client, {n: bundle.setups[n] for n in uploaded_names}, upload_snapshot
            )
            live_globals = device_globals.capture(client)
            if live_globals is not None:
                snapshot = device_globals.dump(live_globals)
                with setup_sync_store.sync_files():
                    (upload_snapshot / 'device-globals.toml').write_bytes(snapshot)
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
                except data_files.InvalidSetupName as error:
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
                    print('Delete: ' + ', '.join(names))
                    try:
                        answer = input('Are you sure? [y/N] ')
                    except EOFError:
                        sys.exit('Input ended; setup deletion cancelled.')
                    if answer.strip().lower() not in ('y', 'yes'):
                        return
                try:
                    client.delete(names)
                except transport.USB_ERRORS:
                    try:
                        remaining = {e.name.lower() for e in client.catalogue()}
                        for n in names:
                            if n.lower() not in remaining:
                                print(
                                    f'{n}: deleted before batch failure',
                                    file=sys.stderr,
                                )
                    except transport.USB_ERRORS as error:
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
    sources: dict[str, object] = {}
    files: dict[Path, bytes] = {}
    for name in pending:
        data_files.validate_name(name)
        if name.lower() not in entries:
            raise transport.UsbReplyError(f'{name}: no such setup')
        name = entries[name.lower()]
        if name in setups:
            continue
        data = client.download(name)
        try:
            text = resolved.export(data)
        except binary_format.DocumentFormatError as error:
            raise transport.UsbReplyError(
                f'{name}: cannot decode setup: {error}'
            ) from error
        source = tomlkit.parse(text).unwrap()
        image_paths, artwork = resolved.artwork_files(data)
        if image_paths:
            from .display import remap_artwork

            remap_artwork(source, image_paths)
            text = tomlkit.dumps(source)
            files.update(artwork)
        setups[name] = data
        sources[name] = source
        files[Path(f'{name}.toml')] = text.encode()
        pending.extend(document_references(source))
    for name, source in sources.items():
        try:
            settings = models.Setup.model_validate(source)
        except ValidationError as error:
            raise transport.UsbReplyError(f'{name}: invalid setup: {error}') from error
        for value in settings.fonts.values():
            path = Path(value)
            if path not in files:
                try:
                    key = int(path.stem, 16)
                except ValueError as error:
                    raise transport.UsbReplyError(
                        f'{name}: invalid font reference {value}'
                    ) from error
                files[path] = client.download_font(key)
        for value in settings.splash_movie + settings.idle_movie:
            path = Path(value)
            if path not in files:
                try:
                    key = int(path.stem, 16)
                except ValueError as error:
                    raise transport.UsbReplyError(
                        f'{name}: invalid movie reference {value}'
                    ) from error
                files[path] = client.download_movie(key)
    for name in sorted({n for s in sources.values() for n in midi_assets.filenames(s)}):
        path = Path(name)
        data = client.download_midi(name)
        if path in files and files[path] != data:
            raise transport.UsbReplyError(
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
            except transport.UsbFileMissing:
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
        try:
            if destination.is_dir():
                errors.append(f'{destination}: is a directory')
            elif not force and (destination.exists() or destination.is_symlink()):
                errors.append(f'{destination}: exists; use --force/-f')
            for parent in destination.parents:
                if (parent.exists() or parent.is_symlink()) and not parent.is_dir():
                    errors.append(f'{parent}: not a directory')
        except OSError as error:
            raise DownloadOutputError(f'{destination}: {error}') from error
        for parent in relative.parents:
            if str(parent).casefold() in paths:
                errors.append(f'{parent}: required as both a file and a directory')
    if errors:
        raise compiler_errors.CompilationError(errors)
    for relative, data in files.items():
        destination = output / relative
        try:
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
        except OSError as error:
            raise DownloadOutputError(f'{destination}: {error}') from error
        print(destination)


def save_asset(destination: Path, data: bytes) -> None:
    try:
        destination.parent.mkdir(exist_ok=True)
        if destination.exists():
            if destination.read_bytes() != data:
                raise CompileOutputError(f'{destination}: asset checksum collision')
            return
        output = NamedTemporaryFile(
            dir=destination.parent, prefix='.cntrl-', delete=False
        )
        temporary = Path(output.name)
        try:
            with output:
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            destination.hardlink_to(temporary)
        finally:
            temporary.unlink(missing_ok=True)
    except OSError as error:
        raise CompileOutputError(f'{destination}: {error}') from error


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
    command: data_files.Sources,
    output: Path,
    *,
    loaded: tuple[SetupInputs, data_files.Trace] | None = None,
) -> setup_bundle.SetupBundle:
    assets: dict[str, dict[int, bytes]] = {}
    movies: dict[int, bytes] = {}
    if loaded is None:
        bundle = compile_inputs(command, assets, movies)
    else:
        bundle = compile_loaded(*loaded, assets, movies)
    data = bundle.pack()
    try:
        for fonts in assets.values():
            for key, font in fonts.items():
                path = output.parent / 'fonts' / f'{key:08x}.cfont'
                if loaded is not None and path.is_file() and path.read_bytes() != font:
                    setup_sync_store.replace(path, font)
                else:
                    save_asset(path, font)
        for key, movie in movies.items():
            path = output.parent / 'movies' / f'{key:08x}.cnsa'
            if loaded is not None and path.is_file() and path.read_bytes() != movie:
                setup_sync_store.replace(path, movie)
            else:
                save_asset(path, movie)
        output.write_bytes(data)
    except OSError as error:
        raise CompileOutputError(str(error)) from error
    return bundle


def main() -> None:
    arguments = sys.argv[1:]
    if all(a in ('--verbose', '--no-verbose') for a in arguments):
        arguments += ['setup', 'create']
    verbose = False
    for argument in arguments:
        if not argument.startswith('-'):
            break
        match argument.replace('_', '-'):
            case '--verbose':
                verbose = True
            case '--no-verbose':
                verbose = False
    with nullcontext() if verbose else _errors.on_parse_error(_show_parse_validation):
        options = tyro.cli(
            CommandLine,
            config=(tyro.conf.OmitSubcommandPrefixes,),
            args=arguments,
        )
    command = options.command
    token = compiler_errors.verbose_validation.set(options.verbose)
    try:
        if isinstance(command, importing.Import):
            importing.run(command)
        elif isinstance(command, Emulate):
            emulate(command, compile_to_file)
        elif isinstance(command, qualify.Qualify):
            qualify.run(command)
        elif isinstance(command, flash.Flash):
            flash.run(command)
        elif isinstance(command, build.Build):
            build.run(command)
        elif isinstance(command, font_coverage.Report):
            font_coverage.run(command)
        elif isinstance(command, Compile):
            report_bundle(compile_to_file(command, command.output))
            if (contact := device_globals.saved_contact()) is not None:
                device_globals.sync_contact(contact, port=command.port)
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
    except (
        AnimationFormatError,
        backups.BackupDirectoryExists,
        backups.BackupError,
        binary_format.DocumentFormatError,
        catalogue.CatalogueError,
        compiler_errors.CompilationError,
        compiler_inputs.DocumentSourceError,
        CompileOutputError,
        data_files.InvalidSetupName,
        device_globals.GlobalRestoreError,
        device_settings.DeviceSettingError,
        devices.DeviceSelectionError,
        DownloadOutputError,
        EmulatorError,
        EmulatorFileError,
        FontDecodeError,
        font_coverage.ReportError,
        history.HistoryError,
        layout.LayoutError,
        midi_assets.MidiAssetMissingError,
        models.MidiFilenameError,
        setup_create.SetupCreationCancelled,
        setup_sync_store.SyncError,
        setup_sync_store.SyncFileError,
        songs.SongLibraryError,
        source_archive.RecoveredSourceError,
        subprocess.CalledProcessError,
        target_process.TargetAdapterError,
        *transport.USB_ERRORS,
        transport.UsbTransferInputError,
        ValidationError,
        WriterLockError,
        WriterLockHeld,
    ) as error:
        message = str(error)
        if not options.verbose:
            # Domain errors can include the original diagnostics alongside a filename
            # or other context. Keep that context when replacing the Pydantic text.
            cause: BaseException | None = error
            while cause is not None:
                if isinstance(cause, ValidationError):
                    message = message.replace(
                        str(cause), compiler_errors.validation_message(cause)
                    )
                cause = cause.__cause__
        sys.exit(message)
    finally:
        compiler_errors.verbose_validation.reset(token)


def _show_parse_validation(event: _errors.ParseErrorEvent) -> None:
    if not isinstance(event, _errors.InstantiationFailure):
        return
    # Tyro retains the original exception as context when wrapping a model failure.
    cause = sys.exception()
    while cause is not None and not isinstance(cause, ValidationError):
        cause = cause.__cause__ or cause.__context__
    if cause is None:
        return
    token = compiler_errors.verbose_validation.set(False)
    try:
        rows = compiler_errors.validation_errors('', cause)
    finally:
        compiler_errors.verbose_validation.reset(token)
    print(
        _fmtlib.box['red'](
            'error', _fmtlib.rows(*rows, 'For full helptext, see cntrl --help')
        ),
        file=sys.stderr,
    )
    sys.exit(2)


if __name__ == '__main__':
    from .. import entrypoint

    entrypoint.main()
