"""Opt-in qualification against a test-owned virtual MIDI destination."""

from uuid import uuid4

from middy import midi

from cntrl.emulator.host_midi import HostMidi


def test_host_cleanup_reaches_the_virtual_destination() -> None:
    name = f'cntrl-cleanup-{uuid4().hex}'
    with midi.Input(raw=True).create_virtual(name) as receiver:
        connection = HostMidi(False, True)
        try:
            connection.refresh()
            indices = [
                i for i, port in enumerate(connection.output_ports) if port.name == name
            ]
            assert len(indices) == 1
            connection.connect_output(indices[0])
            connection.send(b'\x90\x3c\x64\xb0\x40\x7f')
        finally:
            connection.close()

        received = []
        for _ in range(4):
            event = receiver.receive(timeout=1)
            assert event is not None
            received.append(event.packet)
        assert received == [
            b'\x90\x3c\x64',
            b'\xb0\x40\x7f',
            b'\x80\x3c\x00',
            b'\xb0\x40\x00',
        ]
        assert receiver.receive(timeout=0) is None
