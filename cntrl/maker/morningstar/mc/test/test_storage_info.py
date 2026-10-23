"""Decode SD identity registers and filesystem sizes without accessing hardware."""

import pytest

from cntrl.maker.morningstar.mc.storage_info import parse_storage_info

CID = '035344534c333247801234567801a900'


def test_card_identity_and_capacity_do_not_require_free_space_scan() -> None:
    info = parse_storage_info(f'CNTRL STORAGE 67108864 3 {CID} 32 32768 1000000 -')
    assert info.capacity_bytes == 32 * 1024**3
    assert info.card_type == 'SDHC' and info.filesystem == 'FAT32'
    assert info.manufacturer_id == '0x03' and info.oem_id == 'SD'
    assert info.product == 'SL32G' and info.revision == '8.0'
    assert info.serial_number == '0x12345678' and info.manufactured == '2026-09'
    assert info.usable_bytes == 32768 * 1000000
    assert info.allocation_unit_bytes == 32768
    assert info.free_bytes is None and info.used_bytes is None


def test_scan_reports_used_and_free_space_on_sdxc() -> None:
    info = parse_storage_info(f'CNTRL STORAGE 134217728 3 {CID} 64 32768 2000000 1000')
    assert info.card_type == 'SDXC' and info.filesystem == 'exFAT'
    assert info.free_bytes == 32768 * 1000
    assert info.used_bytes == 32768 * 1999000


@pytest.mark.parametrize(
    'response',
    [
        'CNTRL ERROR storage-unavailable',
        'CNTRL STORAGE 1 3 ab 32 32768 100 -',
        f'CNTRL STORAGE 1 3 {CID} 32 32768 100 101',
        f'CNTRL STORAGE 1 0 {CID} 32 32768 100 -',
        f'CNTRL STORAGE 1 3 {CID} 0 32768 100 -',
    ],
)
def test_invalid_storage_responses_are_errors(response: str) -> None:
    with pytest.raises(ValueError):
        parse_storage_info(response)
