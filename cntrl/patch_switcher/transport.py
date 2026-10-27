"""Framed document transfer over a POSIX USB serial device."""

import os
import select
import termios
import time
from pathlib import Path
from zlib import crc32

from pydantic import BaseModel

from .data_files import validate_name


class SerialConnection:
    def __init__(self, port: Path, timeout: float = 30) -> None:
        self.port = port
        self.timeout = timeout
        self.fd = -1
        self.previous: list[int | list[bytes | int]] = []

    def __enter__(self) -> 'SerialConnection':
        self.fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            self.previous = termios.tcgetattr(self.fd)
            settings = termios.tcgetattr(self.fd)
            settings[0] = 0
            settings[1] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[3] = 0
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = 0
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIFLUSH)
        except (OSError, termios.error):
            os.close(self.fd)
            raise
        return self

    def __exit__(self, *args: object) -> None:
        try:
            termios.tcsetattr(self.fd, termios.TCSANOW, self.previous)
        finally:
            os.close(self.fd)

    def write(self, data: bytes) -> None:
        deadline = time.monotonic() + self.timeout
        pending = memoryview(data)
        while pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError('USB serial write timed out')
            try:
                count = os.write(self.fd, pending)
            except BlockingIOError:
                continue
            if count == 0:
                raise OSError('USB serial disconnected')
            pending = pending[count:]

    def line(self, limit: int = 256) -> str:
        deadline = time.monotonic() + self.timeout
        result = bytearray()
        while len(result) < limit:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise TimeoutError('USB serial response timed out')
            try:
                value = os.read(self.fd, 1)
            except BlockingIOError:
                continue
            if not value:
                raise OSError('USB serial disconnected')
            if value == b'\n':
                return result.decode('ascii').rstrip('\r')
            result.extend(value)
        raise ValueError(f'USB serial response exceeds {limit - 1} bytes')


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
        response = self.connection.line(limit=600)
        if response.startswith('CNTRL ERROR'):
            raise ValueError(response)
        return response

    def ok(self, command: str) -> None:
        if (response := self.request(command)) != 'CNTRL OK':
            raise ValueError(f'unexpected response: {response}')

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

    def download(self, name: str) -> bytes:
        response = self.request(
            'CNTRL SETUP GET ' + validate_name(name).encode('ascii').hex()
        )
        parts = response.split()
        if len(parts) != 4 or parts[:2] != ['CNTRL', 'FILE']:
            raise ValueError(f'invalid download response: {response}')
        length, checksum = int(parts[2]), int(parts[3], 16)
        if not 116 <= length <= 65536:
            raise ValueError('invalid download length')
        result = bytearray()
        while len(result) < length:
            response = self.request('CNTRL NEXT')
            if not response.startswith('CNTRL DATA '):
                raise ValueError(response)
            chunk = bytes.fromhex(response[11:])
            if not 1 <= len(chunk) <= 128:
                raise ValueError('invalid download chunk')
            result.extend(chunk)
        if len(result) != length or crc32(result) != checksum:
            raise ValueError('download checksum/length mismatch')
        return bytes(result)

    def delete(self, names: list[str]) -> None:
        payload = b''.join(validate_name(n).encode('ascii') + b'\0' for n in names)
        if not payload or len(payload) > 65536:
            raise ValueError('deletion batch must contain 1..65536 bytes of names')
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
            for i in range(0, len(data), 128):
                self.ok('CNTRL FONT DATA ' + data[i : i + 128].hex())
            self.ok('CNTRL FONT END')
        if self.download_font(key) != data:
            raise ValueError('font asset read-back mismatch or checksum collision')

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
            if not 1 <= len(chunk) <= 128:
                raise ValueError('invalid font asset chunk')
            result.extend(chunk)
        if len(result) != length or crc32(result) != key:
            raise ValueError('font asset checksum/length mismatch')
        return bytes(result)


def device_status(port: Path) -> str:
    with SerialConnection(port) as connection:
        connection.write(b'CNTRL STATUS\n')
        response = connection.line()
        if response.startswith('CNTRL ERROR'):
            raise ValueError(response)
        return response
