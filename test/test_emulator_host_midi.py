"""Pure stream assembly, without opening any host MIDI connection."""

import pytest

pytest.importorskip('middy')

from cntrl.emulator.host_midi import HostMidi


def test_sysex_fragments_keep_realtime_messages_and_endpoint_state_independent() -> (
    None
):
    first, second = HostMidi(False, False), HostMidi(False, False)
    assert first.packets(b'\xf0\x7d\x01') == []
    assert second.packets(b'\xf8') == [b'\xf8']
    assert first.packets(b'\x02\xf8\x03') == [b'\xf8']
    assert first.packets(b'\x04\xf7') == [b'\xf0\x7d\x01\x02\x03\x04\xf7']
    assert first.packets(b'\x90\x3c\x64') == [b'\x90\x3c\x64']


def test_disconnection_discards_an_unfinished_host_sysex_stream() -> None:
    connection = HostMidi(False, False)
    assert connection.packets(b'\xf0\x7d\x01') == []
    connection.close()
    assert connection.packets(b'\x90\x3c\x64') == [b'\x90\x3c\x64']


def test_malformed_output_is_reported_before_it_reaches_a_host_port() -> None:
    connection = HostMidi(False, False)
    with pytest.raises(ValueError, match='invalid outgoing MIDI stream'):
        connection.packets(b'\x01')
    assert connection.packets(b'\xf8') == [b'\xf8']
