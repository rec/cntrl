import signal
from pathlib import Path

from cntrl.patch_switcher import songs
from cntrl.patch_switcher.transport import SerialConnection, SetupClient

def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt

signal.signal(signal.SIGALRM, stop)
signal.alarm(120)
backup = Path(Path('/private/tmp/cntrl-latest-backup-path.txt').read_text())
output = backup / 'format-30'
with SerialConnection(Path('/dev/cu.usbmodem144682801'), timeout=5) as connection:
    client = SetupClient(connection)
    for font in (output / 'fonts').glob('*.cfont'):
        client.copy_font(int(font.stem, 16), font.read_bytes())
    for source in output.glob('*.bin'):
        client.copy(source.stem, source.read_bytes(), overwrite=True)
        assert client.download(source.stem) == source.read_bytes()
        print('Restored and verified', source.stem, flush=True)
    selected = (backup / 'selected.txt').read_text()
    client.select(selected)
    assert client.current() == selected
    assert songs.download(client) == (backup / 'songs.bin').read_bytes()
    catalogue = '\n'.join(f'{e.name} {e.valid}' for e in client.catalogue())
    assert catalogue == (backup / 'catalogue.txt').read_text()
    status = client.request('CNTRL STATUS')
    assert status.startswith('CNTRL READY 30 '), status
    print(status, flush=True)
    print('Selection, songs, and catalogue preserved', flush=True)
signal.alarm(0)
