import signal
import sys
from pathlib import Path
from cntrl.patch_switcher.transport import SerialConnection, SetupClient


def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt


signal.signal(signal.SIGALRM, stop)
signal.alarm(60)
backup = Path('/private/tmp/cntrl-tap-held-before-flash')
record = sys.argv[1] == 'before'
if record:
    backup.mkdir()
with SerialConnection(Path('/dev/cu.usbmodem144682801')) as connection:
    client = SetupClient(connection)
    selected = client.current()
    entries = client.catalogue()
    catalogue = '\n'.join(f'{e.name} {e.valid}' for e in entries)
    if record:
        (backup / 'selected.txt').write_text(selected)
        (backup / 'catalogue.txt').write_text(catalogue)
    else:
        assert selected == (backup / 'selected.txt').read_text()
        assert catalogue == (backup / 'catalogue.txt').read_text()
    for entry in entries:
        if not entry.valid:
            continue
        data = client.download(entry.name)
        path = backup / f'{entry.name}.bin'
        if record:
            path.write_bytes(data)
        else:
            assert data == path.read_bytes(), entry.name
        print(entry.name, len(data), 'saved' if record else 'verified', flush=True)
    print(client.request('CNTRL STATUS'))
    print('Selected', selected)
signal.alarm(0)
