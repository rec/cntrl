from pathlib import Path

import tomlkit

from cntrl.patch_switcher import cli, resolved
from cntrl.patch_switcher.transport import SerialConnection, SetupClient

backup = Path('/private/tmp/cntrl-before-right-pages-20261104')
backup.mkdir(exist_ok=True)
with SerialConnection(Path('/dev/cu.usbmodem144682801')) as connection:
    client = SetupClient(connection)
    print(client.request('CNTRL STATUS'))
    (backup / 'selected.txt').write_text(client.current())
    for entry in client.catalogue():
        if not entry.valid:
            print(f'Preserving invalid setup: {entry.name}')
            continue
        data = client.download(entry.name)
        (backup / f'{entry.name}.bin').write_bytes(data)
        exported = resolved.export(data)
        (backup / f'{entry.name}.toml').write_text(exported)
        source = tomlkit.parse(exported).unwrap()
        for filename in source.get('fonts', {}).values():
            key = int(Path(filename).stem, 16)
            cli.save_font(backup, key, client.download_font(key))
        print(f'Backed up {entry.name}: {len(data)} bytes')
