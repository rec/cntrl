"""Text-framed USB reads for backing up MC firmware before binary USB upgrades."""

import time

from ....patch_switcher.transport import SerialConnection, UsbProtocolError
from ....patch_switcher.usb_frames import Frame, Kind, Status


class Connection(SerialConnection):
    def write_frame(self, data: bytes) -> None:
        super().write_frame(encode_request(Frame.decode(data)))

    def read_frame(self) -> Frame:
        assert self.serial is not None
        self.serial.timeout = self.timeout
        limit = (self.usb_block_size * 2 + 128) * 2 + 64
        data = self.serial.read_until(b'\n', limit)
        return decode_reply(data, self.session, self.sequence)

    def release(self) -> None:
        if self.serial is not None:
            try:
                self.serial.write(
                    encode_request(
                        Frame(kind=Kind.close, session=self.session, sequence=0)
                    )
                )
            except OSError:
                pass  # Cancellation must not wait for an unplugged device.

    def line(self, limit: int | None = None) -> str:
        limit = self.usb_block_size * 2 + 128 if limit is None else limit
        while True:
            if b'\n' in self.frame_received:
                line, _, remaining = self.frame_received.partition(b'\n')
                self.frame_received = remaining
                if len(line) >= limit:
                    raise ValueError('legacy USB response exceeds its line limit')
                response = line.decode('ascii').rstrip('\r')
                if response != 'CNTRL WAIT':
                    return response
            elif self.received:
                self.frame_received.extend(self.received.pop(0).payload)
            else:
                self.exchange(b'', Kind.poll)
                if not self.received:
                    time.sleep(0.01)

    def data(self) -> bytes:
        response = self.line()
        if not response.startswith('CNTRL DATA '):
            raise ValueError(response)
        return bytes.fromhex(response[11:])


def encode_request(frame: Frame) -> bytes:
    if frame.kind not in (Kind.open, Kind.close, Kind.command, Kind.poll):
        raise ValueError('legacy USB is supported only for backup commands')
    return (
        f'\x1eCNTRL USB {frame.session:x} {frame.sequence} '
        f'{frame.acknowledged} {frame.payload.hex() or "-"}\n'
    ).encode('ascii')


def decode_reply(data: bytes, session: int, sequence: int) -> Frame:
    if not data.endswith(b'\n'):
        raise UsbProtocolError(
            'legacy USB response timed out or exceeded its frame limit'
        )
    fields = data.split()
    if fields == [b'CNTRL', b'USB', b'LOST']:
        return Frame(
            kind=Kind.text, status=Status.lost, session=session, sequence=sequence
        )
    if len(fields) != 7 or fields[:2] != [b'CNTRL', b'USB']:
        raise UsbProtocolError(f'invalid legacy USB response: {data!r}')
    block_size = int(fields[5])
    if not 1 <= block_size <= 65535:
        raise UsbProtocolError('invalid legacy USB block size')
    return Frame(
        kind=Kind.text,
        session=int(fields[2], 16),
        sequence=int(fields[3]),
        acknowledged=int(fields[4]),
        block_size=block_size,
        payload=b'' if fields[6] == b'-' else bytes.fromhex(fields[6].decode('ascii')),
    )
