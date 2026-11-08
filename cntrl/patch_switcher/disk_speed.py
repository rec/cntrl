"""Time a verified one-megabyte USB round trip through device storage."""

from pathlib import Path
from time import perf_counter_ns
from zlib import crc32

from pydantic import BaseModel

from .transport import SerialConnection, SetupClient


class Timings(BaseModel, frozen=True):
    upload_ms: float
    download_ms: float

    @property
    def total_ms(self) -> float:
        return self.upload_ms + self.download_ms


def measure(port: Path) -> Timings:
    length = 1_000_000
    data = (bytes(range(256)) * ((length + 255) // 256))[:length]
    checksum = crc32(data)
    with SerialConnection(port) as connection:
        client = SetupClient(connection)
        try:
            start = perf_counter_ns()
            client.ok(f'CNTRL DISK BEGIN {checksum:08x}')
            for i in range(0, length, connection.usb_block_size):
                client.ok(
                    'CNTRL DISK DATA ' + data[i : i + connection.usb_block_size].hex()
                )
            client.ok('CNTRL DISK END')
            uploaded = perf_counter_ns()
            result = bytearray()
            while len(result) < length:
                response = client.request(f'CNTRL DISK READ {len(result)}')
                if not response.startswith('CNTRL DATA '):
                    raise ValueError(response)
                chunk = bytes.fromhex(response[11:])
                if (
                    not 1
                    <= len(chunk)
                    <= min(connection.usb_block_size, length - len(result))
                ):
                    raise ValueError('invalid disk-speed download chunk')
                result.extend(chunk)
            downloaded = perf_counter_ns()
            if result != data:
                raise ValueError('disk-speed read-back verification failed')
            client.ok('CNTRL DISK DELETE')
            return Timings(
                upload_ms=(uploaded - start) / 1_000_000,
                download_ms=(downloaded - uploaded) / 1_000_000,
            )
        finally:
            # Releasing the session also removes an unfinished scratch file,
            # including on control-C, without waiting for a disconnected pedal.
            connection.release()
