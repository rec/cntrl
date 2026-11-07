from pathlib import Path

from cntrl.patch_switcher.transport import SerialConnection, SetupClient

output = Path('/private/tmp/cntrl-right-pages-ready-20261104')
with SerialConnection(Path('/dev/cu.usbmodem144682801')) as connection:
    client = SetupClient(connection)
    print(client.request('CNTRL STATUS'))
    for source in output.glob('*.bin'):
        data = source.read_bytes()
        client.copy(source.stem, data, overwrite=True)
        assert client.download(source.stem) == data
        print(f'Updated and read-back verified {source.stem}: {len(data)} bytes')
    selected = Path('/private/tmp/cntrl-before-right-pages-20261104/selected.txt').read_text()
    client.select(selected)
    assert client.current() == selected
    print(client.request('CNTRL STATUS'))
    print(f'Selected {selected}')
