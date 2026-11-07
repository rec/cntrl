"""Packet decoding and transfer checks without connecting to MIDI devices."""

import ctypes
import struct

import pytest

from cntrl.patch_switcher.core_midi import Midi, exchange


class Echo(Midi):
    def __init__(self, enabled: bool = True) -> None:
        self.data = bytearray()
        self.sent: list[tuple[int, bytes]] = []
        self.enabled = enabled

    def send(self, endpoint: int, data: bytes) -> None:
        self.sent.append((endpoint, data))
        if self.enabled:
            self.data.extend(data)


def test_variable_length_callback_packets_follow_packed_alignment() -> None:
    source = bytearray(struct.pack('<I', 3))
    packets = [b'\xc0\x01', b'\xb0\x07\x64', b'\xf0\x7d\x01\x02\xf7']
    for p in packets:
        source.extend(struct.pack('<QH', 123, len(p)) + p)
        source.extend(bytes(-len(source) % 4))
    buffer = ctypes.create_string_buffer(bytes(source))
    midi = Echo()
    midi.receive(ctypes.addressof(buffer), None, None)
    assert midi.data == b''.join(packets)


def test_transfer_uses_bounded_chunks_and_clears_old_received_data() -> None:
    midi = Echo()
    midi.data.extend(b'old')
    assert (
        exchange(
            midi,
            7,
            b'abcdefg',
            b'abcdefg',
            chunk_bytes=3,
            interval_seconds=0,
            settle_seconds=0,
            label='Echo',
        )
        == b'abcdefg'
    )
    assert midi.sent == [(7, b'abc'), (7, b'def'), (7, b'g')]


def test_disabled_forwarding_can_be_verified_without_an_echo() -> None:
    midi = Echo(False)
    assert (
        exchange(
            midi,
            5,
            b'abc',
            b'',
            chunk_bytes=3,
            interval_seconds=0,
            settle_seconds=0,
            label='Disabled',
        )
        == b''
    )
    with pytest.raises(ValueError, match='Echo mismatch: expected 3, got 0 bytes'):
        exchange(
            midi,
            5,
            b'abc',
            b'abc',
            chunk_bytes=3,
            interval_seconds=0,
            settle_seconds=0,
            label='Echo',
        )
