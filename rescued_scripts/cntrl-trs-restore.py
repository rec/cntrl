import signal
from pathlib import Path

from cntrl.patch_switcher.transport import SerialConnection, SetupClient


def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt


signal.signal(signal.SIGALRM, stop)
signal.alarm(90)
backup = Path('/private/tmp/cntrl-trs-ports-device-backup')
output = backup / 'prepared'
with SerialConnection(Path('/dev/cu.usbmodem144682801')) as connection:
    client = SetupClient(connection)
    for source in output.glob('*.bin'):
        client.copy(source.stem, source.read_bytes(), overwrite=True)
        print('Restored and read-back verified', source.stem, flush=True)
    client.ok('CNTRL SETUP FINISH')
    selected = (backup / 'selected.txt').read_text().strip()
    client.select(selected)
    assert client.current() == selected
    for source in output.glob('*.bin'):
        assert client.download(source.stem) == source.read_bytes()
    entries = client.catalogue()
    assert {e.name for e in entries if e.valid} == {'simple-setup', 'readability-demo'}
    assert {e.name for e in entries if not e.valid} == {'font-demo', 'pman', 'sd-font-demo'}
    status = client.request('CNTRL STATUS')
    assert status.startswith('CNTRL READY 29 '), status
    assert client.request('CNTRL SONGS SIZE') == (backup / 'songs-status.txt').read_text()
    print(status)
    print('Selected', selected)
    print('Both valid setups verified; three previously invalid setups preserved')
signal.alarm(0)
