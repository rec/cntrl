"""Back up the pedal without changing its saved data.

Run from the repository root:
    uv run scripts/backup-pedal.py backups/before-flash --port /dev/cu.usbmodem...

The destination must not exist. It receives selection, catalogue and status text,
the binary song library, and a setups/ directory containing each valid current
setup as binary and editable TOML, with referenced fonts in setups/fonts/.
Invalid setups are listed in the catalogue but are not downloaded. Revision
history, firmware and other SD-card files are outside this backup's scope.
USB errors use the transport's reconnect policy; Control-C cancels the backup.
An interrupted backup leaves its partial files in place.
"""

import sys
from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel

from cntrl.patch_switcher import resolved, songs
from cntrl.patch_switcher.cli import save_font
from cntrl.patch_switcher.transport import SerialConnection, SetupClient


class BackupPedal(BaseModel, frozen=True):
    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


def backup_pedal(directory: Path, port: Path) -> None:
    directory.mkdir(parents=True)
    setups = directory / 'setups'
    setups.mkdir()
    print(f'Backup: {directory}', flush=True)
    with SerialConnection(port, timeout=5) as connection:
        client = SetupClient(connection)
        selected = client.current()
        entries = client.catalogue()
        (directory / 'selected.txt').write_text(selected)
        (directory / 'catalogue.txt').write_text(
            '\n'.join(f'{e.name} {e.valid}' for e in entries)
        )
        status = client.request('CNTRL STATUS')
        (directory / 'status.txt').write_text(status)
        fonts: set[int] = set()
        for entry in entries:
            if not entry.valid:
                print(f'Skipped invalid setup: {entry.name}', flush=True)
                continue
            data = client.download(entry.name)
            (setups / f'{entry.name}.bin').write_bytes(data)
            text = resolved.export(data)
            (setups / f'{entry.name}.toml').write_text(text)
            assets = tomlkit.parse(text).unwrap().get('fonts', {})
            for value in assets.values():
                key = int(Path(value).stem, 16)
                if key not in fonts:
                    save_font(setups, key, client.download_font(key))
                    fonts.add(key)
            print(f'Saved {entry.name}: {len(data)} bytes', flush=True)
        (directory / 'songs.bin').write_bytes(songs.download(client))
    print(f'Selected: {selected}\n{status}\nBackup complete.', flush=True)


def main() -> None:
    options = tyro.cli(BackupPedal)
    try:
        backup_pedal(options.directory, options.port)
    except KeyboardInterrupt:
        sys.exit('Backup cancelled; partial files remain in the destination.')
    except (OSError, ValueError) as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
