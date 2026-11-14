"""Framed document transfer over a USB serial device."""

import secrets
import sys
import time
from pathlib import Path
from zlib import crc32

from pydantic import BaseModel
from serial import Serial, SerialTimeoutException

from . import binary_format
from .data_files import validate_name
from .models import PlayMidiFile, TargetMemory
from .usb_frames import HEADER, MAGIC, Frame, Kind, Status


class UsbProtocolError(ValueError):
    """The device replied, but this host cannot interpret its USB protocol."""


class SerialConnection:
    """A resumable, acknowledged byte stream over a replaceable serial port."""

    def __init__(self, port: Path, timeout: float = 30, *, retry: bool = True) -> None:
        self.port = port
        self.timeout = timeout
        self.retry = retry
        self.connection_attempted = False
        self.usb_block_size = 4096
        self.serial: Serial | None = None
        self.session = secrets.randbits(32) or 1
        self.sequence = 1
        self.acknowledged = 0
        self.received: list[Frame] = []
        self.frame_received = bytearray()

    def __enter__(self) -> 'SerialConnection':
        try:
            self.exchange(b'', Kind.open)
        except (KeyboardInterrupt, UsbProtocolError):
            self.release()
            self.close()
            raise
        return self

    def __exit__(self, *args: object) -> None:
        try:
            if args and args[0] in (KeyboardInterrupt, UsbProtocolError):
                self.release()
            elif args and args[0] is not None:
                if self.serial is not None:
                    try:
                        self.sequence = 0
                        self.exchange(b'', Kind.close, retry=False)
                    except (OSError, UsbProtocolError):
                        self.release()
            elif self.serial is not None:
                self.sequence = 0
                self.exchange(b'', Kind.close)
        finally:
            self.close()

    def release(self) -> None:
        if self.serial is not None:
            try:
                self.serial.write(
                    Frame(kind=Kind.close, session=self.session, sequence=0).encode()
                )
            except OSError:
                pass  # Cancellation must not wait for an unplugged device.

    def close(self) -> None:
        self.frame_received.clear()
        if self.serial is None:
            return
        port, self.serial = self.serial, None
        try:
            port.close()
        except OSError:
            pass  # The USB device may have disappeared during the command.

    def connect(self) -> None:
        self.serial = Serial(
            str(self.port),
            baudrate=115200,
            timeout=self.timeout,
            write_timeout=self.timeout,
        )
        self.serial.reset_input_buffer()
        self.frame_received.clear()

    def exchange(
        self, data: bytes, kind: Kind = Kind.command, *, retry: bool = True
    ) -> None:
        frame = Frame(
            kind=kind,
            session=self.session,
            sequence=self.sequence,
            acknowledged=self.acknowledged,
            payload=data,
        ).encode()
        failures = 0
        delay = 1
        lost_reported = False
        while True:
            try:
                if self.serial is None:
                    if not retry or (not self.retry and self.connection_attempted):
                        raise OSError('USB connection is closed')
                    self.connection_attempted = True
                    self.connect()
                self.write_frame(frame)
                while True:
                    response = self.read_frame()
                    raw_response = response.encode()
                    if (
                        response.session != self.session
                        or response.sequence < self.sequence
                    ):
                        continue
                    if response.status == Status.lost:
                        if self.sequence == 0:
                            return
                        if not lost_reported:
                            print(
                                'USB session was lost; the unfinished command will not '
                                'be replayed. Hit control-C to exit.',
                                file=sys.stderr,
                                flush=True,
                            )
                            lost_reported = True
                        raise OSError('USB session lost')
                    if response.status != Status.ok or response.kind not in (
                        Kind.text,
                        Kind.data,
                    ):
                        raise self.protocol_error(
                            f'Binary USB status={response.status.name}, '
                            f'kind={response.kind.name}',
                            raw_response,
                        )
                    if response.sequence != self.sequence:
                        raise self.protocol_error(
                            'Unexpected USB sequence', raw_response
                        )
                    if (
                        response.payload
                        and response.acknowledged == self.acknowledged + 1
                    ):
                        self.received.append(response)
                        self.acknowledged = response.acknowledged
                    elif (
                        response.payload and response.acknowledged != self.acknowledged
                    ):
                        raise self.protocol_error(
                            'Unexpected USB reply sequence', raw_response
                        )
                    self.usb_block_size = response.block_size
                    self.sequence += 1
                    return
            except OSError as error:
                self.close()
                if not retry or not self.retry:
                    raise
                failures += 1
                if failures >= 3:
                    print(
                        f'Waiting {delay} seconds for USB: {error} '
                        '(hit control-C to exit)',
                        file=sys.stderr,
                        flush=True,
                    )
                    # Short sleeps remain interruptible even with a very large
                    # accumulated backoff, without overflowing platform time_t.
                    for _ in range(delay):
                        time.sleep(1)
                    delay *= 2

    def protocol_error(self, reason: str, response: bytes) -> UsbProtocolError:
        return UsbProtocolError(
            f'ERROR: {reason}\n'
            f'Port: {self.port}\n'
            f'Host transport: {__file__}\n'
            'Expected reply: binary CNTRL version 1, '
            '40-byte CRC-protected header and raw payload\n'
            f'Host state: session={self.session:x}, sequence={self.sequence}, '
            f'acknowledged_reply={self.acknowledged}, '
            f'usb_block_size={self.usb_block_size}\n'
            f'Received (length={len(response)}): {response!r}'
        )

    def write_frame(self, data: bytes) -> None:
        assert self.serial is not None
        deadline = time.monotonic() + self.timeout
        pending = memoryview(data)
        while pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('USB serial write timed out')
            self.serial.write_timeout = remaining
            try:
                count = self.serial.write(pending)
            except SerialTimeoutException as error:
                raise TimeoutError('USB serial write timed out') from error
            if count == 0:
                raise OSError('USB serial disconnected')
            pending = pending[count:]

    def read_frame(self) -> Frame:
        assert self.serial is not None
        deadline = time.monotonic() + self.timeout
        discarded = bytearray()
        failure = (
            'expected a binary CNTRL frame; install matching binary-protocol firmware'
        )
        while True:
            limit = HEADER.size + max(1024, self.usb_block_size)
            if self.frame_received and not MAGIC.startswith(
                self.frame_received[: len(MAGIC)]
            ):
                discarded.extend(self.frame_received[:1])
                del self.frame_received[:1]
                if len(discarded) > limit:
                    raise self.protocol_error(failure, bytes(discarded))
                continue
            if len(self.frame_received) >= HEADER.size:
                header = bytes(self.frame_received[: HEADER.size])
                fields = HEADER.unpack(header)
                if crc32(header[:-4]) != fields[-1]:
                    failure = 'invalid binary USB header CRC'
                    discarded.extend(self.frame_received[:1])
                    del self.frame_received[:1]
                    continue
                if fields[1] != 1 or not 1 <= fields[7] <= 65535:
                    raise self.protocol_error(
                        f'unsupported USB version={fields[1]} '
                        f'or block_size={fields[7]}',
                        header,
                    )
                if fields[8] > max(1024, fields[7]):
                    raise self.protocol_error(
                        f'invalid binary USB payload length {fields[8]}', header
                    )
                limit = HEADER.size + fields[8]
                if len(self.frame_received) >= limit:
                    raw = bytes(self.frame_received[:limit])
                    if crc32(raw[HEADER.size :]) != fields[9]:
                        failure = 'invalid binary USB payload CRC'
                        discarded.extend(self.frame_received[:1])
                        del self.frame_received[:1]
                        continue
                    del self.frame_received[:limit]
                    try:
                        return Frame.decode(raw)
                    except ValueError as error:
                        raise self.protocol_error(str(error), raw) from error
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                if discarded or self.sequence == 1:
                    raise self.protocol_error(
                        failure, bytes(discarded + self.frame_received)
                    )
                raise TimeoutError('USB serial response timed out')
            available = self.serial.in_waiting
            if not available:
                self.serial.timeout = remaining
            count = min(max(1, available), max(1, limit - len(self.frame_received)))
            value = self.serial.read(count)
            if not value:
                if discarded or self.sequence == 1:
                    raise self.protocol_error(
                        failure, bytes(discarded + self.frame_received)
                    )
                raise TimeoutError('USB serial response timed out')
            self.frame_received.extend(value)

    def write(self, data: bytes, kind: Kind = Kind.command) -> None:
        for i in range(0, len(data), self.usb_block_size):
            self.exchange(data[i : i + self.usb_block_size], kind)

    def line(self, limit: int | None = None) -> str:
        if limit is None:
            limit = 1024
        while True:
            if self.received:
                frame = self.received.pop(0)
                if frame.kind != Kind.text:
                    raise self.protocol_error(
                        'expected text reply, received binary data', frame.encode()
                    )
                line = frame.payload.rstrip(b'\r\n')
                if len(line) >= limit:
                    raise ValueError(f'USB serial response exceeds {limit - 1} bytes')
                response = line.decode('ascii').rstrip('\r')
                if response != 'CNTRL WAIT':
                    return response
            else:
                self.exchange(b'', Kind.poll)
                if not self.received:
                    time.sleep(0.01)

    def data(self) -> bytes:
        while True:
            if self.received:
                frame = self.received.pop(0)
                if frame.kind == Kind.data:
                    return frame.payload
                text = frame.payload.decode('ascii').rstrip('\r\n')
                if text != 'CNTRL WAIT':
                    raise ValueError(text)
            else:
                self.exchange(b'', Kind.poll)


class SetupEntry(BaseModel, frozen=True):
    name: str
    modified: int
    length: int
    checksum: int
    valid: bool = True


class SetupClient:
    def __init__(self, connection: SerialConnection) -> None:
        self.connection = connection

    def request(self, command: str) -> str:
        self.connection.write((command + '\n').encode('ascii'))
        response = self.connection.line()
        if response.startswith('CNTRL ERROR'):
            raise ValueError(response)
        return response

    def ok(self, command: str) -> None:
        if (response := self.request(command)) != 'CNTRL OK':
            raise ValueError(f'unexpected response: {response}')

    def send_data(self, kind: Kind, data: bytes) -> None:
        self.connection.write(data, kind)
        if (response := self.connection.line()) != 'CNTRL OK':
            raise ValueError(response)

    def request_data(self, command: str) -> bytes:
        self.connection.write((command + '\n').encode('ascii'))
        return self.connection.data()

    def memory(self) -> TargetMemory:
        response = self.request('CNTRL MEMORY')
        fields = response.split()
        if len(fields) != 4 or fields[:2] != ['CNTRL', 'MEMORY']:
            raise ValueError(f'invalid memory response: {response}')
        return TargetMemory(capacity=int(fields[2]), runtime=int(fields[3]))

    def catalogue(self) -> list[SetupEntry]:
        result: list[SetupEntry] = []
        response = self.request('CNTRL SETUP LIST')
        while response != 'CNTRL END':
            parts = response.split()
            if len(parts) == 3 and parts[:2] == ['CNTRL', 'INVALID']:
                result.append(
                    SetupEntry(
                        name=validate_name(bytes.fromhex(parts[2]).decode('ascii')),
                        modified=0,
                        length=0,
                        checksum=0,
                        valid=False,
                    )
                )
                response = self.request('CNTRL NEXT')
                continue
            if len(parts) != 6 or parts[:2] != ['CNTRL', 'ENTRY']:
                raise ValueError(f'invalid catalogue entry: {response}')
            result.append(
                SetupEntry(
                    name=validate_name(bytes.fromhex(parts[2]).decode('ascii')),
                    modified=int(parts[3]),
                    length=int(parts[4]),
                    checksum=int(parts[5], 16),
                )
            )
            response = self.request('CNTRL NEXT')
        return result

    def current(self) -> str:
        response = self.request('CNTRL SETUP CURRENT')
        if not response.startswith('CNTRL CURRENT '):
            raise ValueError(f'invalid selection response: {response}')
        return bytes.fromhex(response[14:]).decode('ascii')

    def select(self, name: str) -> None:
        self.ok('CNTRL SETUP SET ' + validate_name(name).encode('ascii').hex())

    def copy(self, name: str, data: bytes, overwrite: bool) -> None:
        if data.startswith(binary_format.MAGIC):
            binary_format.unpack(data, binary_format.Kind.setup)
            if (
                self.request('CNTRL FORMAT')
                != f'CNTRL FORMAT {binary_format.DEVELOPMENT_FORMAT} 1'
            ):
                raise ValueError(
                    'firmware does not support the persistent document format'
                )
        hex_name = validate_name(name).encode('ascii').hex()
        command = (
            f'CNTRL SETUP PUT {hex_name} {len(data)} {crc32(data):08x} {int(overwrite)}'
        )
        if (response := self.request(command)) != 'CNTRL SEND':
            raise ValueError(response)
        self.connection.write(data, Kind.setup)
        if (response := self.connection.line()) != 'CNTRL OK':
            raise ValueError(response)
        # Read back the committed bytes, not merely the acknowledged checksum.
        if self.download(name) != data:
            raise ValueError(f'{name}: read-back verification failed')

    def copy_bundle(self, data: bytes, overwrite: bool) -> None:
        from .setup_bundle import unpack

        if any(d.startswith(binary_format.MAGIC) for d in unpack(data).values()):
            if (
                self.request('CNTRL FORMAT')
                != f'CNTRL FORMAT {binary_format.DEVELOPMENT_FORMAT} 1'
            ):
                raise ValueError(
                    'firmware does not support the persistent document format'
                )
        response = self.request(
            f'CNTRL BUNDLE BEGIN {len(data)} {crc32(data):08x} {int(overwrite)}'
        )
        if response != 'CNTRL SEND':
            raise ValueError(response)
        for i in range(0, len(data), self.connection.usb_block_size):
            self.send_data(Kind.bundle, data[i : i + self.connection.usb_block_size])
        self.ok('CNTRL BUNDLE END')
        if self.download_bundle() != data:
            raise ValueError('bundle read-back verification failed')

    def begin_deployment(self) -> int:
        state = self.request('CNTRL DEPLOY STATUS').split()
        if (
            len(state) != 5
            or state[:2] != ['CNTRL', 'DEPLOY']
            or state[3:] != ['0', '1']
        ):
            raise ValueError(
                'device requires deployment recovery before another update'
            )
        root = secrets.randbelow(0xFFFFFFFF) + 1
        self.ok(f'CNTRL DEPLOY BEGIN {root:08x}')
        return root

    def recover_deployment(self) -> None:
        state = self.request('CNTRL DEPLOY STATUS').split()
        if len(state) != 5 or state[:2] != ['CNTRL', 'DEPLOY'] or state[4] != '1':
            raise ValueError(
                'select a known-good root with cntrl setup recover before restoring'
            )
        if state[3] == '1':
            self.ok('CNTRL DEPLOY ABORT')

    def commit_deployment(self, bundle: bytes, library: bytes) -> None:
        self.ok(f'CNTRL DEPLOY COMMIT {crc32(bundle):08x} {crc32(library):08x}')

    def download_starts(self) -> bytes:
        response = self.request('CNTRL STARTS SIZE').split()
        if len(response) != 4 or response[:3] != ['CNTRL', 'STARTS', 'SIZE']:
            raise ValueError('invalid startup counter size response')
        size = int(response[3])
        if not 0 < size <= 1024:
            raise ValueError('invalid startup counter size')
        data = bytearray()
        while len(data) < size:
            part = self.request_data(f'CNTRL STARTS READ {len(data)}')
            if not 0 < len(part) <= size - len(data):
                raise ValueError('invalid startup counter read')
            data.extend(part)
        return bytes(data)

    def download_state(self, path: str) -> bytes:
        encoded = path.encode('ascii').hex()
        response = self.request(f'CNTRL STATE SIZE {encoded}')
        if response == 'CNTRL STATE MISSING':
            raise FileNotFoundError(f'{path}: device state is missing')
        fields = response.split()
        if len(fields) != 3 or fields[:2] != ['CNTRL', 'STATE']:
            raise ValueError(f'{path}: invalid device state size response')
        size = int(fields[2])
        if not 0 <= size <= 0xFFFFFFFF:
            raise ValueError(f'{path}: invalid device state size')
        result = bytearray()
        while len(result) < size:
            part = self.request_data(f'CNTRL STATE READ {encoded} {len(result)}')
            if (
                not 0
                < len(part)
                <= min(self.connection.usb_block_size, size - len(result))
            ):
                raise ValueError(f'{path}: invalid device state read')
            result.extend(part)
        return bytes(result)

    def download_bundle(self) -> bytes:
        parts = self.request('CNTRL BUNDLE SIZE').split()
        if len(parts) != 4 or parts[:2] != ['CNTRL', 'BUNDLE']:
            raise ValueError('invalid bundle size response')
        length, checksum = int(parts[2]), int(parts[3], 16)
        result = bytearray()
        for i in range(0, length, self.connection.usb_block_size):
            count = min(self.connection.usb_block_size, length - i)
            result.extend(self.request_data(f'CNTRL BUNDLE GET {i} {count}'))
        if len(result) != length or crc32(result) != checksum:
            raise ValueError('bundle checksum mismatch')
        return bytes(result)

    def download(self, name: str, revision: int | None = None) -> bytes:
        hex_name = validate_name(name).encode('ascii').hex()
        response = self.request(
            ('CNTRL SETUP GET ' + hex_name)
            if revision is None
            else f'CNTRL HISTORY GET {hex_name} {revision}'
        )
        parts = response.split()
        if len(parts) != 4 or parts[:2] != ['CNTRL', 'FILE']:
            raise ValueError(f'invalid download response: {response}')
        length, checksum = int(parts[2]), int(parts[3], 16)
        if not 116 <= length <= 0xFFFFFFFF - 100:
            raise ValueError('invalid download length')
        result = bytearray()
        while len(result) < length:
            chunk = self.request_data('CNTRL NEXT')
            if not 1 <= len(chunk) <= self.connection.usb_block_size:
                raise ValueError('invalid download chunk')
            result.extend(chunk)
        if len(result) != length or crc32(result) != checksum:
            raise ValueError('download checksum/length mismatch')
        return bytes(result)

    def delete(self, names: list[str]) -> None:
        payload = b''.join(validate_name(n).encode('ascii') + b'\0' for n in names)
        if not payload or len(payload) > 0xFFFFFFFF - 100:
            raise ValueError('deletion batch must fit a nonempty 32-bit storage record')
        response = self.request(
            f'CNTRL SETUP DELETE {len(payload)} {crc32(payload):08x}'
        )
        if response != 'CNTRL SEND':
            raise ValueError(response)
        self.connection.write(payload, Kind.setup)
        if (response := self.connection.line()) != 'CNTRL OK':
            raise ValueError(response)

    def copy_midi(self, name: str, data: bytes, overwrite: bool) -> None:
        PlayMidiFile.basename(name)
        if (
            not 1 <= len(name) <= 95
            or name.lower().startswith('.cntrl-')
            or not 0 < len(data) <= 0xFFFFFFFF
        ):
            raise ValueError('invalid MIDI file name or size')
        try:
            existing = self.download_midi(name)
        except FileNotFoundError:
            existing = None
        if existing == data:
            return
        if existing is not None and not overwrite:
            raise ValueError(
                f'{name}: MIDI file exists with different contents; use --overwrite/-o'
            )
        encoded = name.encode('ascii').hex()
        response = self.request(
            f'CNTRL MIDI_FILE BEGIN {encoded} {len(data)} '
            f'{crc32(data):08x} {int(overwrite)}'
        )
        if response != 'CNTRL SEND':
            raise ValueError(response)
        for i in range(0, len(data), self.connection.usb_block_size):
            self.send_data(Kind.midi_file, data[i : i + self.connection.usb_block_size])
        self.ok('CNTRL MIDI_FILE END')
        if self.download_midi(name) != data:
            raise ValueError(f'{name}: MIDI file read-back mismatch')

    def download_midi(self, name: str) -> bytes:
        PlayMidiFile.basename(name)
        if not 1 <= len(name) <= 95 or name.lower().startswith('.cntrl-'):
            raise ValueError('invalid MIDI file name')
        encoded = name.encode('ascii').hex()
        response = self.request(f'CNTRL MIDI_FILE SIZE {encoded}')
        if response == 'CNTRL MIDI_FILE MISSING':
            raise FileNotFoundError(f'{name}: MIDI file is missing on the pedal')
        parts = response.split()
        if len(parts) != 3 or parts[:2] != ['CNTRL', 'MIDI_FILE']:
            raise ValueError(f'invalid MIDI file response: {response}')
        length = int(parts[2])
        if not 0 < length <= 0xFFFFFFFF:
            raise ValueError('invalid MIDI file length')
        result = bytearray()
        while len(result) < length:
            chunk = self.request_data(f'CNTRL MIDI_FILE READ {encoded} {len(result)}')
            if (
                not 0
                < len(chunk)
                <= min(self.connection.usb_block_size, length - len(result))
            ):
                raise ValueError('invalid MIDI file chunk')
            result.extend(chunk)
        return bytes(result)

    def copy_font(self, key: int, data: bytes) -> None:
        if not 8 <= len(data) <= 262144 or not key or crc32(data) != key:
            raise ValueError('invalid font asset size/checksum')
        response = self.request(f'CNTRL FONT BEGIN {key:08x} {len(data)}')
        if response not in ('CNTRL SEND', 'CNTRL EXISTS'):
            raise ValueError(response)
        if response == 'CNTRL SEND':
            for i in range(0, len(data), self.connection.usb_block_size):
                self.send_data(Kind.font, data[i : i + self.connection.usb_block_size])
            self.ok('CNTRL FONT END')
        if self.download_font(key) != data:
            raise ValueError('font asset read-back mismatch or checksum collision')

    def copy_movie(self, data: bytes) -> str:
        from ..animation.asset import inspect_asset

        inspect_asset(data)
        key = crc32(data)
        response = self.request(f'CNTRL SPLASH BEGIN {len(data)} {key:08x}')
        if response == 'CNTRL SPLASH SEND':
            for i in range(0, len(data), self.connection.usb_block_size):
                self.connection.write(
                    data[i : i + self.connection.usb_block_size], Kind.movie
                )
                received = min(i + self.connection.usb_block_size, len(data))
                expected = (
                    f'CNTRL SPLASH NEXT {received}'
                    if received < len(data)
                    else f'CNTRL SPLASH DONE {len(data)} {key:08x}'
                )
                if (response := self.connection.line()) != expected:
                    raise ValueError(response)
        elif response != 'CNTRL SPLASH EXISTS':
            raise ValueError(response)
        if self.download_movie(key) != data:
            raise ValueError('movie asset read-back mismatch or checksum collision')
        return f'CNTRL SPLASH READY {len(data)} {key:08x}'

    def download_movie(self, key: int) -> bytes:
        from ..animation.asset import HEADER

        response = self.request(f'CNTRL SPLASH SIZE {key:08x}')
        if response == 'CNTRL SPLASH ERROR missing':
            raise FileNotFoundError(f'{key:08x}: device cannot read the stored movie')
        parts = response.split()
        if len(parts) != 4 or parts[:3] != ['CNTRL', 'SPLASH', 'SIZE']:
            raise ValueError(f'invalid movie response: {response}')
        length = int(parts[3])
        if not HEADER.size < length <= 0xFFFFFFFF - 20:
            raise ValueError('invalid movie asset length')
        result = bytearray()
        while len(result) < length:
            chunk = self.request_data(f'CNTRL SPLASH READ {key:08x} {len(result)}')
            if (
                not 1
                <= len(chunk)
                <= min(self.connection.usb_block_size, length - len(result))
            ):
                raise ValueError('invalid movie asset chunk')
            result.extend(chunk)
        if crc32(result) != key:
            raise ValueError('movie asset checksum mismatch')
        return bytes(result)

    def download_font(self, key: int) -> bytes:
        response = self.request(f'CNTRL FONT SIZE {key:08x}')
        parts = response.split()
        if len(parts) != 3 or parts[:2] != ['CNTRL', 'FONT']:
            raise ValueError(f'invalid font response: {response}')
        length = int(parts[2])
        if not 8 <= length <= 262144:
            raise ValueError('invalid font asset length')
        result = bytearray()
        while len(result) < length:
            chunk = self.request_data(f'CNTRL FONT READ {key:08x} {len(result)}')
            if not 1 <= len(chunk) <= self.connection.usb_block_size:
                raise ValueError('invalid font asset chunk')
            result.extend(chunk)
        if len(result) != length or crc32(result) != key:
            raise ValueError('font asset checksum/length mismatch')
        return bytes(result)


def device_status(port: Path, reset_starts: bool = False) -> str:
    with SerialConnection(port) as connection:
        client = SetupClient(connection)
        if reset_starts:
            client.ok('CNTRL STARTS RESET')
        return client.request('CNTRL STATUS')
