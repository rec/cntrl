import hashlib
import struct
from pathlib import Path

import pytest

from cntrl.maker.morningstar.mc.firmware import inspect_firmware, read_hex


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


def test_inspection_writes_analysis_artifacts_without_changing_source(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    binary = bytearray(0x2000)
    binary[:4] = b'FCFB'
    struct.pack_into('<I', binary, 0x50, 8 * 1024 * 1024)
    struct.pack_into(
        '<8I',
        binary,
        0x1000,
        0x432000D1,
        0x60001201,
        0,
        0,
        0x60001020,
        0x60001000,
        0x60001400,
        0,
    )
    binary[0x1200:0x1208] = b'LCD Test'
    records = [hex_record(4, bytes.fromhex('6000'))]
    records.extend(
        hex_record(0, bytes(binary[i : i + 16]), i) for i in range(0, len(binary), 16)
    )
    records.extend([hex_record(5, bytes.fromhex('60001000')), hex_record(1)])
    path = tmp_path / 'image.hex'
    path.write_text('\n'.join(records))
    original = path.read_bytes()
    output = tmp_path / 'artifacts'
    inspect_firmware(path, output)
    assert path.read_bytes() == original
    assert output.joinpath('firmware.bin').read_bytes() == binary
    elf = output.joinpath('firmware.elf').read_bytes()
    assert elf[:4] == b'\x7fELF'
    assert struct.unpack_from('<H', elf, 18)[0] == 40
    assert struct.unpack_from('<I', elf, 24)[0] == 0x60001201
    assert elf[52 : 52 + len(binary)] == binary
    assert output.joinpath('strings.txt').read_text() == '0x60001200: LCD Test\n'
    assert 'CSF remainder all zero: True' in capsys.readouterr().out
    with pytest.raises(FileExistsError):
        inspect_firmware(path, output)
