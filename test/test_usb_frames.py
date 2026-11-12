"""Binary frames preserve raw bytes and reject corrupted headers and payloads."""

from io import BytesIO
from pathlib import Path
from unittest.mock import Mock

import pytest

from cntrl.patch_switcher.transport import SerialConnection, UsbProtocolError
from cntrl.patch_switcher.usb_frames import MAGIC, Frame, Kind


@pytest.mark.parametrize('failure_during_connect', [False, True])
def test_nonretrying_session_attempts_one_connection_and_preserves_io_failure(
    failure_during_connect: bool, monkeypatch: pytest.MonkeyPatch
) -> None:
    connection = SerialConnection(Path('unused'), retry=False)
    failure = OSError('USB device disappeared')
    backend = Mock()
    backend.write.side_effect = failure
    attempts = 0

    def connect() -> None:
        nonlocal attempts
        attempts += 1
        if failure_during_connect:
            raise failure
        connection.serial = backend

    monkeypatch.setattr(connection, 'connect', connect)
    with pytest.raises(OSError) as caught:
        connection.exchange(b'', Kind.open)
    assert caught.value is failure and attempts == 1
    assert connection.serial is None
    with pytest.raises(OSError, match='connection is closed'):
        connection.exchange(b'CNTRL QUAL STOP\n')
    assert attempts == 1
    if not failure_during_connect:
        backend.close.assert_called_once()


def test_explicit_release_and_close_exits_without_a_new_handshake(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    connection = SerialConnection(Path('unused'), retry=False)
    connection.session = 42
    wire = Frame(kind=Kind.text, session=42, sequence=1, block_size=4096).encode()
    source = BytesIO(wire)
    backend = Mock(in_waiting=len(wire))
    backend.read.side_effect = source.read
    backend.write.side_effect = lambda data: len(data)
    connection.serial = backend

    def unexpected_reconnect() -> None:
        pytest.fail('An explicitly closed session must not reconnect')

    monkeypatch.setattr(connection, 'connect', unexpected_reconnect)
    with connection:
        connection.release()
        connection.close()
    assert backend.write.call_count == 2
    assert Frame.decode(bytes(backend.write.call_args.args[0])).kind == Kind.close
    backend.close.assert_called_once()


def test_raw_payload_preserves_all_byte_values_and_embedded_frame_magic() -> None:
    frame = Frame(
        kind=Kind.data,
        session=42,
        sequence=7,
        block_size=4096,
        payload=bytes(range(256)) + MAGIC,
    )
    wire = frame.encode()
    assert len(wire) == 40 + len(frame.payload)
    assert wire[40:] == frame.payload
    assert Frame.decode(wire) == frame


@pytest.mark.parametrize('offset', [8, 16, 28, 36, 40, 100])
def test_corrupted_frame_is_rejected(offset: int) -> None:
    wire = bytearray(
        Frame(
            kind=Kind.data, session=42, sequence=7, payload=bytes(range(256))
        ).encode()
    )
    wire[offset] ^= 1
    with pytest.raises(ValueError, match='CRC'):
        Frame.decode(bytes(wire))


@pytest.mark.parametrize('damage', ['header', 'payload', 'truncated'])
def test_reader_resynchronizes_before_a_valid_retry(damage: str) -> None:
    frame = Frame(
        kind=Kind.data,
        session=42,
        sequence=7,
        block_size=4096,
        payload=bytes(range(256)) * 16,
    )
    damaged = bytearray(frame.encode())
    if damage == 'truncated':
        del damaged[53:]
    else:
        damaged[16 if damage == 'header' else 100] ^= 1
    wire = bytes(damaged) + frame.encode()
    source = BytesIO(wire)
    backend = Mock(in_waiting=len(wire))
    backend.read.side_effect = source.read
    connection = SerialConnection(Path('unused'))
    connection.serial = backend
    assert connection.read_frame() == frame


def test_old_text_firmware_reports_matching_firmware_requirement() -> None:
    source = BytesIO(b'CNTRL READY old firmware\n')
    backend = Mock(in_waiting=100)
    backend.read.side_effect = source.read
    connection = SerialConnection(Path('unused'))
    connection.serial = backend
    with pytest.raises(
        UsbProtocolError, match='install matching binary-protocol firmware'
    ):
        connection.read_frame()


def test_incomplete_reply_in_an_open_session_remains_retryable() -> None:
    frame = Frame(
        kind=Kind.data,
        session=42,
        sequence=2,
        block_size=4096,
        payload=bytes(range(256)),
    )
    source = BytesIO(frame.encode()[:53])
    backend = Mock(in_waiting=100)
    backend.read.side_effect = source.read
    connection = SerialConnection(Path('unused'))
    connection.sequence = 2
    connection.serial = backend
    with pytest.raises(TimeoutError, match='response timed out'):
        connection.read_frame()


@pytest.mark.parametrize(
    'failure',
    [
        ValueError('note check mismatch'),
        RuntimeError('verification failed'),
        SystemExit(1),
    ],
)
def test_body_failure_preserves_original_error_when_usb_disappears(
    failure: BaseException, monkeypatch: pytest.MonkeyPatch
) -> None:
    connection = SerialConnection(Path('unused'))
    connection.session = 42
    wire = Frame(kind=Kind.text, session=42, sequence=1, block_size=4096).encode()
    source = BytesIO(wire)
    backend = Mock(in_waiting=len(wire))
    backend.read.side_effect = source.read
    backend.write.side_effect = lambda data: len(data)
    connection.serial = backend

    def unexpected_reconnect() -> None:
        pytest.fail('Failure cleanup must not reconnect')

    monkeypatch.setattr(connection, 'connect', unexpected_reconnect)
    with pytest.raises(type(failure)) as caught, connection:
        backend.write.side_effect = OSError('USB device disappeared')
        raise failure
    assert caught.value is failure
    assert connection.serial is None
    backend.close.assert_called_once()
    assert backend.write.call_count == 2
    released = Frame.decode(bytes(backend.write.call_args.args[0]))
    assert released.kind == Kind.close and released.sequence == 0
