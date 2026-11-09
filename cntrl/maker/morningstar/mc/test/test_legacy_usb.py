"""The pre-binary USB codec preserves session ordering and exact backup bytes."""

from pathlib import Path

import pytest

from cntrl.maker.morningstar.mc.legacy_usb import (
    Connection,
    decode_reply,
    encode_request,
)
from cntrl.patch_switcher.transport import UsbProtocolError
from cntrl.patch_switcher.usb_frames import Frame, Kind, Status


def test_backup_command_uses_the_original_text_envelope() -> None:
    frame = Frame(
        kind=Kind.command,
        session=0x12AB,
        sequence=3,
        acknowledged=2,
        payload=b'CNTRL STATUS\n',
    )
    assert (
        encode_request(frame) == b'\x1eCNTRL USB 12ab 3 2 434e54524c205354415455530a\n'
    )


def test_backup_replies_keep_the_device_session_and_reply_sequence() -> None:
    result = decode_reply(b'CNTRL USB 12ab 3 9 4096 434e54524c204f4b0a\r\n', 1, 1)
    assert result == Frame(
        kind=Kind.text,
        session=0x12AB,
        sequence=3,
        acknowledged=9,
        block_size=4096,
        payload=b'CNTRL OK\n',
    )


def test_partial_lines_and_hex_data_survive_frame_boundaries() -> None:
    connection = Connection(Path('/unused'))
    connection.received = [
        Frame(kind=Kind.text, session=1, sequence=1, payload=b'CNTRL WAIT\nCNTRL DA'),
        Frame(kind=Kind.text, session=1, sequence=2, payload=b'TA 00ff1020\n'),
    ]
    assert connection.data() == b'\x00\xff\x10\x20'


def test_advertised_large_blocks_are_preserved_without_truncation() -> None:
    payload = b'CNTRL DATA ' + bytes(range(256)).hex().encode() * 16 + b'\n'
    reply = b'CNTRL USB 1 2 3 4096 ' + payload.hex().encode() + b'\n'
    frame = decode_reply(reply, 1, 2)
    assert frame.block_size == 4096 and frame.payload == payload
    connection = Connection(Path('/unused'))
    connection.usb_block_size = frame.block_size
    connection.received = [frame]
    assert connection.data() == bytes(range(256)) * 16


def test_lost_session_is_not_replayed_as_a_new_session() -> None:
    result = decode_reply(b'CNTRL USB LOST\n', 7, 4)
    assert result.status == Status.lost and result.session == 7 and result.sequence == 4


@pytest.mark.parametrize('reply', [b'', b'CNTRL USB 1 2 0 -', b'wrong\n'])
def test_invalid_or_incomplete_text_replies_are_rejected(reply: bytes) -> None:
    with pytest.raises(UsbProtocolError):
        decode_reply(reply, 1, 2)


def test_backup_transport_rejects_binary_write_operations() -> None:
    with pytest.raises(ValueError, match='only for backup'):
        encode_request(Frame(kind=Kind.setup, session=1, sequence=2, payload=b'write'))
