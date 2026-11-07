from pathlib import Path

from cntrl.patch_switcher.transport import SerialConnection, SetupClient

snapshot = Path('/private/tmp/cntrl-pause-snapshot-20261104')
with SerialConnection(Path('/dev/cu.usbmodem144682801')) as connection:
    client = SetupClient(connection)
    print(client.request('CNTRL STATUS'))
    selected = client.current()
    verify = snapshot.exists()
    if verify:
        assert (snapshot / 'selected.txt').read_text() == selected
    else:
        snapshot.mkdir()
        (snapshot / 'selected.txt').write_text(selected)
    for entry in client.catalogue():
        if not entry.valid:
            continue
        data = client.download(entry.name)
        path = snapshot / f'{entry.name}.bin'
        if verify:
            assert data == path.read_bytes()
            print(f'Unchanged setup verified: {entry.name}, {len(data)} bytes')
        else:
            path.write_bytes(data)
            print(f'Snapshotted {entry.name}, {len(data)} bytes')
    print(f'Selected: {selected}')
