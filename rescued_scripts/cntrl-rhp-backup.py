import signal
from pathlib import Path
from tempfile import mkdtemp
import tomlkit
from cntrl.patch_switcher.transport import SerialConnection, SetupClient
from cntrl.patch_switcher.resolved import export

def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt
signal.signal(signal.SIGALRM, stop)
signal.alarm(60)
backup = Path(mkdtemp(prefix='cntrl-rhp-', dir='/private/tmp'))
with SerialConnection(Path('/dev/cu.usbmodem144682801')) as connection:
    client = SetupClient(connection)
    (backup / 'selected.txt').write_text(client.current())
    for entry in client.catalogue():
        if not entry.valid:
            continue
        data = client.download(entry.name)
        (backup / f'{entry.name}.bin').write_bytes(data)
        source = export(data)
        (backup / f'{entry.name}.toml').write_text(source)
        for font in tomlkit.parse(source).get('fonts', {}).values():
            path = backup / font
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(client.download_font(int(path.stem, 16)))
        print('Saved', entry.name, len(data))
print('BACKUP', backup)
signal.alarm(0)
