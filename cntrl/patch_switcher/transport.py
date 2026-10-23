"""Framed document transfer over a POSIX USB serial device."""

import os
import select
import termios
import time
from pathlib import Path
from zlib import crc32


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


def upload_document(port: Path, data: bytes) -> str:
    checksum = crc32(data)
    with SerialConnection(port) as connection:
        connection.write(f'CNTRL BEGIN {len(data)} {checksum:08x}\n'.encode())
        response = connection.line()
        if response != 'CNTRL SEND':
            raise ValueError(response)
        connection.write(data)
        response = connection.line()
        if response != f'CNTRL DONE {len(data)} {checksum:08x}':
            raise ValueError(response)
        connection.write(b'CNTRL STATUS\n')
        response = connection.line()
        if response != f'CNTRL READY 6 {len(data)} {checksum:08x}':
            raise ValueError(f'upload verification failed: {response}')
    return response


def device_status(port: Path) -> str:
    with SerialConnection(port) as connection:
        connection.write(b'CNTRL STATUS\n')
        return connection.line()
