import hashlib
from pathlib import Path

import pytest

from cntrl.firmware.intel_hex import encode_hex, read_hex


def hex_record(kind: int, payload: bytes = b'', address: int = 0) -> str:
    record = (
        bytes([len(payload)]) + address.to_bytes(2, 'big') + bytes([kind]) + payload
    )
    return ':' + (record + bytes([-sum(record) % 256])).hex().upper()


def test_hex_decodes_sparse_regions_and_entry(tmp_path: Path) -> None:
    path = tmp_path / 'image.hex'
    source = '\n'.join(
        [
            hex_record(4, bytes.fromhex('6000')),
            hex_record(0, b'ABC', 0x10),
            hex_record(0, b'XYZ', 0x20),
            hex_record(5, bytes.fromhex('60000011')),
            hex_record(1),
        ]
    )
    path.write_text(source)
    image = read_hex(path)
    assert image.origin == 0x60000010
    assert image.entry == 0x60000011
    assert image.data == b'ABC' + b'\xff' * 13 + b'XYZ'
    assert image.regions[0].start == 0x60000010
    assert image.regions[0].end == 0x60000013
    assert image.regions[1].start == 0x60000020
    assert image.regions[1].end == 0x60000023
    assert image.record_counts[0] == 2
    assert image.sha256 == hashlib.sha256(path.read_bytes()).hexdigest()


def test_hex_supports_segment_addresses(tmp_path: Path) -> None:
    path = tmp_path / 'image.hex'
    path.write_text(
        '\n'.join(
            [
                hex_record(2, bytes.fromhex('1234')),
                hex_record(0, b'ABC', 0x10),
                hex_record(1),
            ]
        )
    )
    image = read_hex(path)
    assert image.origin == 0x12350
    assert image.entry is None


@pytest.mark.parametrize(
    'records,error',
    [
        ([':010000004100'], 'checksum'),
        ([':0200000041BD'], 'length'),
        ([hex_record(0, b'A'), hex_record(0, b'A'), hex_record(1)], 'overlapping'),
        ([hex_record(0, b'A')], 'EOF'),
        ([hex_record(0, b'A'), hex_record(1), hex_record(0, b'A')], 'after EOF'),
        ([hex_record(4, b'A'), hex_record(1)], 'malformed'),
    ],
)
def test_hex_rejects_invalid_records(
    tmp_path: Path, records: list[str], error: str
) -> None:
    path = tmp_path / 'image.hex'
    path.write_text('\n'.join(records))
    with pytest.raises(ValueError, match=error):
        read_hex(path)


@pytest.mark.parametrize(('origin', 'entry'), [(0x1234FFF9, 0x12350001), (0, None)])
def test_writer_round_trips_explicit_addresses(
    tmp_path: Path, origin: int, entry: int | None
) -> None:
    data = bytes(range(40))
    source = encode_hex(data, origin, entry)
    path = tmp_path / 'written.hex'
    path.write_text(source)
    image = read_hex(path)
    assert image.origin == origin
    assert image.entry == entry
    assert image.data == data
    assert image.regions == [image.regions[0]]
    assert image.regions[0].start == origin and image.regions[0].end == origin + len(
        data
    )


def test_writer_preserves_known_record_encoding() -> None:
    assert encode_hex(bytes(range(4)), 0x60000000, 0x60001000) == (
        ':0200000460009A\n:0400000000010203F6\n:040000056000100087\n:00000001FF\n'
    )
