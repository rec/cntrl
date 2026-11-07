"""Restore and verify pedal backups without recompiling saved setups."""

from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel

from . import compiler, resolved, songs, transport


class Restore(BaseModel, frozen=True):
    """Restore setups, fonts and saved selection; keep songs and unrelated setups."""

    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


class Verify(BaseModel, frozen=True):
    """Compare the flashed pedal with a backup without changing saved data."""

    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


def restore_setups(directory: Path, port: Path) -> None:
    folder = directory / 'setups'
    selected = (directory / 'selected.txt').read_text()
    setups = read_setup_binaries(directory)
    if selected and selected not in setups:
        raise ValueError(f'{selected}: saved selection is absent from the backup')
    fonts = {
        int(f.stem, 16): f.read_bytes()
        for f in sorted((folder / 'fonts').glob('*.cfont'))
    }
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        for key, data in fonts.items():
            client.copy_font(key, data)
        for name, data in setups.items():
            client.copy(name, data, overwrite=True)
            print(f'{name}: restored and read-back verified', flush=True)
        if setups:
            client.ok('CNTRL SETUP FINISH')
        if selected:
            client.select(selected)
            if client.current() != selected:
                raise ValueError('restored selection does not match the backup')
            print(f'Selected: {selected}', flush=True)


def verify_after_flashing(directory: Path, port: Path) -> None:
    setups = read_setup_binaries(directory)
    selected = (directory / 'selected.txt').read_text()
    catalogue = (directory / 'catalogue.txt').read_text()
    library = (directory / 'songs.bin').read_bytes()
    fonts = {
        int(f.stem, 16): f.read_bytes()
        for f in sorted((directory / 'setups/fonts').glob('*.cfont'))
    }
    with transport.SerialConnection(port) as connection:
        client = transport.SetupClient(connection)
        status = client.request('CNTRL STATUS')
        parts = status.split()
        if len(parts) != 5 or parts[:3] != [
            'CNTRL',
            'READY',
            str(compiler.DOCUMENT_VERSION),
        ]:
            raise ValueError(
                f'expected READY format {compiler.DOCUMENT_VERSION}; received {status}'
            )
        actual = client.current()
        if actual != selected:
            raise ValueError(
                f'selection differs: expected {selected!r}, received {actual!r}'
            )
        actual_catalogue = '\n'.join(f'{e.name} {e.valid}' for e in client.catalogue())
        if actual_catalogue != catalogue:
            raise ValueError(
                f'setup catalogue differs:\nExpected:\n{catalogue}\n'
                f'Received:\n{actual_catalogue}'
            )
        for name, data in setups.items():
            if client.download(name) != data:
                raise ValueError(f'{name}: setup binary differs from the backup')
            print(f'{name}: verified identical', flush=True)
        for key, data in fonts.items():
            if client.download_font(key) != data:
                raise ValueError(f'{key:08x}: font differs from the backup')
        if songs.download(client) != library:
            raise ValueError('song library differs from the backup')
    print(
        f'{status}\nSelection, catalogue, fonts and songs verified unchanged.',
        flush=True,
    )


def read_setup_binaries(directory: Path) -> dict[str, bytes]:
    folder = directory / 'setups'
    if not folder.is_dir():
        raise ValueError(f'{folder}: missing backup setup directory')
    setups = {s.stem: s.read_bytes() for s in sorted(folder.glob('*.bin'))}
    for data in setups.values():
        resolved.graph_offset(data)
    return setups
