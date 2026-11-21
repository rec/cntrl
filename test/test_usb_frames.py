"""Binary frames preserve raw bytes and reject corrupted headers and payloads."""

from io import BytesIO
from pathlib import Path
from unittest.mock import Mock

import pytest

from cntrl.patch_chooser import transport
from cntrl.patch_chooser.transport import SerialConnection, UsbProtocolError
from cntrl.patch_chooser.usb_frames import MAGIC, Frame, Kind


@pytest.mark.parametrize('reader', ['line', 'data'])
def test_invalid_text_reply_reports_a_usb_protocol_error(reader: str) -> None:
    connection = SerialConnection(Path('/unused'), retry=False)
    connection.received.append(
        Frame(kind=Kind.text, session=1, sequence=1, payload=b'\xff')
    )
    with pytest.raises(UsbProtocolError, match='USB text reply is not ASCII'):
        getattr(connection, reader)()


def test_unattended_connection_stops_retrying_at_its_deadline(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(transport.sys.stdin, 'isatty', lambda: False)
    clock = Mock()
    seconds = 0.0

    def sleep(duration: float) -> None:
        nonlocal seconds
        seconds += duration

    clock.monotonic.side_effect = lambda: seconds
    clock.sleep.side_effect = sleep
    monkeypatch.setattr(transport, 'time', clock)
    unplugged = Mock(side_effect=FileNotFoundError('USB unplugged'))
    monkeypatch.setattr(transport, 'Serial', unplugged)

    with pytest.raises(transport.UsbCommandTimeout) as caught:
        with SerialConnection(Path('unused'), timeout=1.5):
            pytest.fail('A disconnected device cannot open a USB session')
    assert type(caught.value) is transport.UsbCommandTimeout
    assert seconds == 1.5
    assert unplugged.call_count == 4


@pytest.mark.parametrize('reply', ['line', 'data'])
@pytest.mark.parametrize('payload', [b'', b'CNTRL WAIT\n'])
def test_waiting_for_a_reply_shares_the_command_write_deadline(
    reply: str, payload: bytes, monkeypatch: pytest.MonkeyPatch
) -> None:
    clock = Mock()
    seconds = 0.0

    def sleep(duration: float) -> None:
        nonlocal seconds
        seconds += duration

    clock.monotonic.side_effect = lambda: seconds
    clock.sleep.side_effect = sleep
    monkeypatch.setattr(transport, 'time', clock)
    connection = SerialConnection(Path('unused'), timeout=1, retry=False)
    connection.serial = Mock()

    def write(raw: bytes) -> None:
        frame = Frame.decode(raw)
        sleep(0.75 if frame.kind == Kind.command else 0.125)

    def read() -> Frame:
        return Frame(
            kind=Kind.text,
            session=connection.session,
            sequence=connection.sequence,
            acknowledged=connection.acknowledged + 1,
            payload=payload,
        )

    monkeypatch.setattr(connection, 'write_frame', write)
    monkeypatch.setattr(connection, 'read_frame', read)
    connection.write(b'CNTRL SETUP FORWARD\n')
    with pytest.raises(transport.UsbCommandOutcomeUnknown, match='may have reached'):
        getattr(connection, reply)()
    assert 1 <= seconds < 1.125


@pytest.mark.parametrize('failure_during_connect', [False, True])
def test_nonretrying_session_attempts_one_connection_and_preserves_io_failure(
    failure_during_connect: bool, monkeypatch: pytest.MonkeyPatch
) -> None:
    connection = SerialConnection(Path('unused'), retry=False)
    failure = OSError('USB device disappeared')
    backend = Mock()
    backend.write.side_effect = failure
    connect = Mock(
        return_value=backend, side_effect=failure if failure_during_connect else None
    )
    monkeypatch.setattr(transport, 'Serial', connect)
    with pytest.raises(
        transport.UsbConnectionError, match='USB device disappeared'
    ) as caught:
        connection.exchange(b'', Kind.open)
    assert caught.value.__cause__ is failure
    assert connection.serial is None
    with pytest.raises(transport.UsbConnectionError, match='connection is closed'):
        connection.exchange(b'CNTRL QUAL STOP\n')
    connect.assert_called_once()
    if not failure_during_connect:
        backend.close.assert_called_once()


def test_host_io_failure_during_handshake_does_not_retry(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    connection = SerialConnection(Path('unused'))
    backend = Mock()
    backend.write.side_effect = len
    connection.serial = backend
    failure = OSError('could not save serial trace')
    monkeypatch.setattr(connection, 'read_frame', Mock(side_effect=failure))
    reconnect = Mock(side_effect=AssertionError('unexpected USB reconnect'))
    monkeypatch.setattr(transport, 'Serial', reconnect)

    with pytest.raises(OSError) as caught, connection:
        pytest.fail('A failed USB handshake cannot enter the session')

    assert caught.value is failure
    reconnect.assert_not_called()
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
