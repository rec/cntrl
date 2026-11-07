"""Restore current setup binaries and fonts from a pedal backup."""

from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel

from . import resolved, transport


class Restore(BaseModel, frozen=True):
    """Restore setups, fonts and saved selection; keep songs and unrelated setups."""

    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


def restore_setups(directory: Path, port: Path) -> None:
    folder = directory / 'setups'
    if not folder.is_dir():
        raise ValueError(f'{folder}: missing backup setup directory')
    selected = (directory / 'selected.txt').read_text()
    setups = {s.stem: s.read_bytes() for s in sorted(folder.glob('*.bin'))}
    # Reject incompatible backup binaries before uploading any assets.
    for data in setups.values():
        resolved.graph_offset(data)
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
