import signal
from pathlib import Path
from cntrl.patch_switcher import songs
from cntrl.patch_switcher.transport import SerialConnection, SetupClient


def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt


signal.signal(signal.SIGALRM, stop)
signal.alarm(90)
backup = Path(Path('/private/tmp/cntrl-latest-backup-path.txt').read_text())
with SerialConnection(Path('/dev/cu.usbmodem144682801'), timeout=5) as connection:
    client = SetupClient(connection)
    status = client.request('CNTRL STATUS')
    assert status.startswith('CNTRL READY 29 '), status
    assert client.current() == (backup / 'selected.txt').read_text()
    catalogue = '\n'.join(f'{e.name} {e.valid}' for e in client.catalogue())
    assert catalogue == (backup / 'catalogue.txt').read_text()
    for source in backup.glob('*.bin'):
        if source.name == 'songs.bin':
            assert songs.download(client) == source.read_bytes()
        else:
            assert client.download(source.stem) == source.read_bytes(), source.stem
            print('Verified unchanged', source.stem, flush=True)
    print(status, flush=True)
    print('Selected', client.current(), flush=True)
    print('Song library and setup catalogue verified unchanged', flush=True)
signal.alarm(0)
