"""Backend deadlines and short I/O retain the framed protocol semantics."""

from pathlib import Path
from unittest.mock import Mock

import pytest
from serial import SerialTimeoutException

from cntrl.patch_switcher import transport


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
    connection = transport.SerialConnection(Path('unused'))
    backend = Mock()
    backend.read.side_effect = [bytes([c]) for c in b'CNTRL OK\r\nnext']
    connection.serial = backend
    assert connection.read_frame() == 'CNTRL OK'
    assert backend.read.call_count == len(b'CNTRL OK\r\n')
