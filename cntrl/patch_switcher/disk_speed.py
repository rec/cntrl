"""Measure USB disk throughput with a verified one-megabyte round trip."""

from pathlib import Path
from time import perf_counter_ns
from zlib import crc32

from pydantic import BaseModel
from tqdm.std import tqdm

from .transport import SerialConnection, SetupClient


class Throughput(BaseModel, frozen=True):
    upload_bytes_per_second: float
    download_bytes_per_second: float
    round_trip_bytes_per_second: float


def measure(port: Path) -> Throughput:
    length = 1_000_000
    data = (bytes(range(256)) * ((length + 255) // 256))[:length]
    checksum = crc32(data)
    with SerialConnection(port) as connection:
        client = SetupClient(connection)
        try:
            with tqdm(
                total=length, desc='Upload', unit='B', unit_scale=True, disable=None
            ) as progress:
                start = perf_counter_ns()
                client.ok(f'CNTRL DISK BEGIN {checksum:08x}')
                for i in range(0, length, connection.usb_block_size):
                    chunk = data[i : i + connection.usb_block_size]
                    client.ok('CNTRL DISK DATA ' + chunk.hex())
                    progress.update(len(chunk))
                client.ok('CNTRL DISK END')
                upload_ns = perf_counter_ns() - start
            result = bytearray()
            with tqdm(
                total=length, desc='Download', unit='B', unit_scale=True, disable=None
            ) as progress:
                start = perf_counter_ns()
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
                    progress.update(len(chunk))
                download_ns = perf_counter_ns() - start
            if result != data:
                raise ValueError('disk-speed read-back verification failed')
            client.ok('CNTRL DISK DELETE')
            return Throughput(
                upload_bytes_per_second=length * 1_000_000_000 / upload_ns,
                download_bytes_per_second=length * 1_000_000_000 / download_ns,
                round_trip_bytes_per_second=2
                * length
                * 1_000_000_000
                / (upload_ns + download_ns),
            )
        finally:
            # Releasing the session also removes an unfinished scratch file,
            # including on control-C, without waiting for a disconnected pedal.
            connection.release()
