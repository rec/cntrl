import signal
from pathlib import Path
from tempfile import mkdtemp
import tomlkit
from cntrl.patch_switcher import resolved, songs
from cntrl.patch_switcher.transport import SerialConnection, SetupClient


def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt


signal.signal(signal.SIGALRM, stop)
signal.alarm(120)
backup = Path(mkdtemp(prefix='cntrl-before-latest-', dir='/private/tmp'))
Path('/private/tmp/cntrl-latest-backup-path.txt').write_text(str(backup))
print('Backup', backup, flush=True)
with SerialConnection(Path('/dev/cu.usbmodem144682801'), timeout=5) as connection:
    client = SetupClient(connection)
    selected = client.current()
    entries = client.catalogue()
    (backup / 'selected.txt').write_text(selected)
    (backup / 'catalogue.txt').write_text('\n'.join(f'{e.name} {e.valid}' for e in entries))
    (backup / 'status.txt').write_text(client.request('CNTRL STATUS'))
    for entry in entries:
        if not entry.valid:
            continue
        data = client.download(entry.name)
        (backup / f'{entry.name}.bin').write_bytes(data)
        text = resolved.export(data)
        (backup / f'{entry.name}.toml').write_text(text)
        for value in tomlkit.parse(text).unwrap().get('fonts', {}).values():
            path = backup / value
            if not path.exists():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(client.download_font(int(path.stem, 16)))
        print('Saved', entry.name, len(data), flush=True)
    (backup / 'songs.bin').write_bytes(songs.download(client))
    print('Selected', selected, flush=True)
    print(client.request('CNTRL STATUS'), flush=True)
signal.alarm(0)
