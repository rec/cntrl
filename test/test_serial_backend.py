"""Backend deadlines and short I/O retain the framed protocol semantics."""

from io import BytesIO
from pathlib import Path
from unittest.mock import Mock

import pytest
from serial import SerialTimeoutException

from cntrl.patch_switcher import transport
from cntrl.patch_switcher.usb_frames import Frame, Kind


def test_serial_endpoint_and_line_coding_are_passed_without_path_resolution(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    backend = Mock()
    factory = Mock(return_value=backend)
    monkeypatch.setattr(transport, 'Serial', factory)
    connection = transport.SerialConnection(Path('COM10'), timeout=2)
    connection.connect()
    factory.assert_called_once_with(
        'COM10', baudrate=115200, timeout=2, write_timeout=2
    )
    backend.reset_input_buffer.assert_called_once_with()
    connection.close()
    backend.close.assert_called_once_with()
    assert connection.serial is None


def test_partial_writes_preserve_all_bytes() -> None:
    connection = transport.SerialConnection(Path('unused'))
    backend = Mock()
    backend.write.side_effect = [2, 3]
    connection.serial = backend
    connection.write_frame(b'abcde')
    assert [bytes(c.args[0]) for c in backend.write.call_args_list] == [
        b'abcde',
        b'cde',
    ]
    assert 0 < backend.write_timeout <= connection.timeout


@pytest.mark.parametrize('failure', [0, SerialTimeoutException('full')])
def test_failed_writes_raise_transport_errors(
    failure: int | SerialTimeoutException,
) -> None:
    connection = transport.SerialConnection(Path('unused'))
    backend = Mock()
    backend.write.side_effect = [failure]
    connection.serial = backend
    with pytest.raises(OSError):
        connection.write_frame(b'frame')


def test_fragmented_reply_is_decoded_without_reading_the_next_frame() -> None:
    frame = Frame(
        kind=Kind.text, session=1, sequence=1, block_size=4096, payload=b'CNTRL OK\n'
    )
    connection = transport.SerialConnection(Path('unused'))
    backend = Mock()
    backend.in_waiting = 0
    backend.read.side_effect = [bytes([c]) for c in frame.encode() + b'next']
    connection.serial = backend
    assert connection.read_frame() == frame
    assert backend.read.call_count == len(frame.encode())


def test_queued_frames_are_read_in_bulk_without_losing_the_next_reply() -> None:
    data = Frame(
        kind=Kind.data,
        session=1,
        sequence=1,
        block_size=4096,
        payload=bytes(range(256)) * 16,
    )
    ok = Frame(
        kind=Kind.text, session=1, sequence=2, block_size=4096, payload=b'CNTRL OK\n'
    )
    wire = data.encode() + ok.encode()
    source = BytesIO(wire)
    backend = Mock(in_waiting=len(wire))
    backend.read.side_effect = source.read
    connection = transport.SerialConnection(Path('unused'))
    connection.serial = backend
    assert connection.read_frame() == data
    assert connection.read_frame() == ok
    assert backend.read.call_count <= 3


def test_closing_discards_prefetched_replies_before_reconnecting() -> None:
    first = Frame(
        kind=Kind.text, session=1, sequence=1, block_size=4096, payload=b'first'
    )
    stale = first.model_copy(update={'payload': b'stale'})
    fresh = first.model_copy(update={'payload': b'fresh'})
    source = BytesIO(first.encode() + stale.encode())
    backend = Mock(in_waiting=100)
    backend.read.side_effect = source.read
    connection = transport.SerialConnection(Path('unused'))
    connection.serial = backend
    assert connection.read_frame() == first
    connection.close()
    replacement = Mock(in_waiting=100)
    replacement.read.side_effect = BytesIO(fresh.encode()).read
    connection.serial = replacement
    assert connection.read_frame() == fresh
