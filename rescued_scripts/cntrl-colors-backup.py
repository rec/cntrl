from pathlib import Path
import importlib.util

from cntrl.patch_switcher import cli, compiler, resolved
from cntrl.patch_switcher.transport import SerialConnection, SetupClient

spec = importlib.util.spec_from_file_location('cntrl.patch_switcher.backup_right_pages', '/private/tmp/cntrl-colors-old-right-pages.py')
old_settings = importlib.util.module_from_spec(spec)
spec.loader.exec_module(old_settings)
resolved.right_pages = old_settings
resolved.DOCUMENT_VERSION = 25

backup = Path('/private/tmp/cntrl-before-colors-20261104')
with SerialConnection(Path('/dev/cu.usbmodem144682801')) as connection:
    backup.mkdir(exist_ok=False)
    client = SetupClient(connection)
    print(client.request('CNTRL STATUS'))
    (backup / 'selected.txt').write_text(client.current())
    for entry in client.catalogue():
        if not entry.valid:
            print(f'Preserving invalid setup: {entry.name}')
            continue
        data = client.download(entry.name)
        (backup / f'{entry.name}.bin').write_bytes(data)
        path = backup / f'{entry.name}.toml'
        path.write_text(resolved.export(data))
        source = compiler.read_document(path)
        for filename in source.get('fonts', {}).values():
            key = int(Path(filename).stem, 16)
            cli.save_font(backup, key, client.download_font(key))
        print(f'Backed up {entry.name}')
