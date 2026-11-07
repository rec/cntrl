import struct
from pathlib import Path

import pytest

from cntrl.maker.morningstar.mc.firmware import inspect_firmware


def hex_record(kind: int, payload: bytes = b'', address: int = 0) -> str:
    record = (
        bytes([len(payload)]) + address.to_bytes(2, 'big') + bytes([kind]) + payload
    )
    return ':' + (record + bytes([-sum(record) % 256])).hex().upper()


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
