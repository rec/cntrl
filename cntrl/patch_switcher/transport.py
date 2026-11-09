"""Framed document transfer over a USB serial device."""

import secrets
import sys
import time
from pathlib import Path
from zlib import crc32

from pydantic import BaseModel
from serial import Serial, SerialTimeoutException

from .data_files import validate_name
from .models import TargetMemory


class UsbProtocolError(ValueError):
    """The device replied, but this host cannot interpret its USB protocol."""


class SerialConnection:
    """A resumable, acknowledged byte stream over a replaceable serial port."""

    def __init__(self, port: Path, timeout: float = 30) -> None:
        self.port = port
        self.timeout = timeout
        self.usb_block_size = 4096
        self.serial: Serial | None = None
        self.session = secrets.randbits(32) or 1
        self.sequence = 1
        self.acknowledged = 0
        self.received = bytearray()

    def __enter__(self) -> 'SerialConnection':
        try:
            self.exchange(b'')
        except (KeyboardInterrupt, UsbProtocolError):
            self.release()
            self.close()
            raise
        return self

    def __exit__(self, *args: object) -> None:
        try:
            if args and args[0] in (KeyboardInterrupt, UsbProtocolError):
                self.release()
            else:
                self.sequence = 0
                self.exchange(b'')
        finally:
            self.close()

    def release(self) -> None:
        if self.serial is not None:
            try:
                self.serial.write(f'\x1eCNTRL USB {self.session:x} 0 0 -\n'.encode())
            except OSError:
                pass  # Cancellation must not wait for an unplugged device.

    def close(self) -> None:
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

    def exchange(self, data: bytes) -> None:
        frame = (
            f'\x1eCNTRL USB {self.session:x} {self.sequence} '
            f'{self.acknowledged} {data.hex() or "-"}\n'
        ).encode('ascii')
        failures = 0
        delay = 1
        lost_reported = False
        while True:
            try:
                if self.serial is None:
                    self.connect()
                self.write_frame(frame)
                while True:
                    response = self.read_frame().split()
                    if response == ['CNTRL', 'USB', 'LOST']:
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
                    if len(response) == 6 and response[:2] == ['CNTRL', 'USB']:
                        raise UsbProtocolError(
                            'USB protocol mismatch: the connected device is running '
                            'older cntrl firmware. Update its firmware before using '
                            'this version of cntrl.'
                        )
                    if len(response) != 7 or response[:2] != ['CNTRL', 'USB']:
                        raise UsbProtocolError('Invalid USB transport response')
                    try:
                        session = int(response[2], 16)
                        sequence, reply = int(response[3]), int(response[4])
                        block_size = int(response[5])
                        if not 1 <= block_size <= 65535:
                            raise ValueError('invalid USB block size')
                        payload = (
                            b'' if response[6] == '-' else bytes.fromhex(response[6])
                        )
                    except ValueError as error:
                        raise UsbProtocolError(
                            'Invalid USB transport response'
                        ) from error
                    if session != self.session or sequence < self.sequence:
                        continue
                    if sequence != self.sequence:
                        raise UsbProtocolError('Unexpected USB sequence')
                    if payload and reply == self.acknowledged + 1:
                        self.received.extend(payload)
                        self.acknowledged = reply
                    elif payload and reply != self.acknowledged:
                        raise UsbProtocolError('Unexpected USB reply sequence')
                    self.usb_block_size = block_size
                    self.sequence += 1
                    return
            except OSError as error:
                self.close()
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

    def read_frame(self) -> str:
        assert self.serial is not None
        deadline = time.monotonic() + self.timeout
        result = bytearray()
        while len(result) < (self.usb_block_size * 2 + 128) * 2 + 64:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('USB serial response timed out')
            if not self.serial.in_waiting:
                self.serial.timeout = remaining
            value = self.serial.read(1)
            if not value:
                raise TimeoutError('USB serial response timed out')
            if value == b'\n':
                try:
                    return result.decode('ascii').rstrip('\r')
                except UnicodeDecodeError as error:
                    raise UsbProtocolError('Invalid USB response encoding') from error
            result.extend(value)
        raise UsbProtocolError('USB transport response too long')

    def write(self, data: bytes) -> None:
        for i in range(0, len(data), self.usb_block_size):
            self.exchange(data[i : i + self.usb_block_size])

    def line(self, limit: int | None = None) -> str:
        if limit is None:
            limit = self.usb_block_size * 2 + 128
        while True:
            if b'\n' in self.received:
                line, _, remaining = self.received.partition(b'\n')
                self.received = remaining
                if len(line) >= limit:
                    raise ValueError(f'USB serial response exceeds {limit - 1} bytes')
                response = line.decode('ascii').rstrip('\r')
                if response != 'CNTRL WAIT':
                    return response
            else:
                self.exchange(b'')
                if not self.received:
                    time.sleep(0.01)


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
        hex_name = validate_name(name).encode('ascii').hex()
        command = (
            f'CNTRL SETUP PUT {hex_name} {len(data)} {crc32(data):08x} {int(overwrite)}'
        )
        if (response := self.request(command)) != 'CNTRL SEND':
            raise ValueError(response)
        self.connection.write(data)
        if (response := self.connection.line()) != 'CNTRL OK':
            raise ValueError(response)
        # Read back the committed bytes, not merely the acknowledged checksum.
        if self.download(name) != data:
            raise ValueError(f'{name}: read-back verification failed')

    def copy_bundle(self, data: bytes, overwrite: bool) -> None:
        response = self.request(
            f'CNTRL BUNDLE BEGIN {len(data)} {crc32(data):08x} {int(overwrite)}'
        )
        if response != 'CNTRL SEND':
            raise ValueError(response)
        for i in range(0, len(data), self.connection.usb_block_size):
            self.ok(
                'CNTRL BUNDLE DATA '
                + data[i : i + self.connection.usb_block_size].hex()
            )
        self.ok('CNTRL BUNDLE END')
        if self.download_bundle() != data:
            raise ValueError('bundle read-back verification failed')

    def download_bundle(self) -> bytes:
        parts = self.request('CNTRL BUNDLE SIZE').split()
        if len(parts) != 4 or parts[:2] != ['CNTRL', 'BUNDLE']:
            raise ValueError('invalid bundle size response')
        length, checksum = int(parts[2]), int(parts[3], 16)
        result = bytearray()
        for i in range(0, length, self.connection.usb_block_size):
            count = min(self.connection.usb_block_size, length - i)
            response = self.request(f'CNTRL BUNDLE GET {i} {count}')
            if not response.startswith('CNTRL BUNDLE '):
                raise ValueError('invalid bundle data response')
            result.extend(bytes.fromhex(response[13:]))
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
            response = self.request('CNTRL NEXT')
            if not response.startswith('CNTRL DATA '):
                raise ValueError(response)
            chunk = bytes.fromhex(response[11:])
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
        self.connection.write(payload)
        if (response := self.connection.line()) != 'CNTRL OK':
            raise ValueError(response)

    def copy_font(self, key: int, data: bytes) -> None:
        if not 8 <= len(data) <= 262144 or not key or crc32(data) != key:
            raise ValueError('invalid font asset size/checksum')
        response = self.request(f'CNTRL FONT BEGIN {key:08x} {len(data)}')
        if response not in ('CNTRL SEND', 'CNTRL EXISTS'):
            raise ValueError(response)
        if response == 'CNTRL SEND':
            for i in range(0, len(data), self.connection.usb_block_size):
                self.ok(
                    'CNTRL FONT DATA '
                    + data[i : i + self.connection.usb_block_size].hex()
                )
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
                self.connection.write(data[i : i + self.connection.usb_block_size])
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
        parts = response.split()
        if len(parts) != 4 or parts[:3] != ['CNTRL', 'SPLASH', 'SIZE']:
            raise ValueError(f'invalid movie response: {response}')
        length = int(parts[3])
        if not HEADER.size < length <= 0xFFFFFFFF - 20:
            raise ValueError('invalid movie asset length')
        result = bytearray()
        while len(result) < length:
            response = self.request(f'CNTRL SPLASH READ {key:08x} {len(result)}')
            if not response.startswith('CNTRL DATA '):
                raise ValueError(response)
            chunk = bytes.fromhex(response[11:])
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
            response = self.request(f'CNTRL FONT READ {key:08x} {len(result)}')
            if not response.startswith('CNTRL DATA '):
                raise ValueError(response)
            chunk = bytes.fromhex(response[11:])
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
