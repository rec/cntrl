"""Decode SD identity registers and filesystem sizes without accessing hardware."""

import pytest

from cntrl.maker.morningstar.mc.storage_info import (
    parse_storage_info,
    read_storage_report,
)

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


def test_listing_reconstructs_paths_and_totals_without_double_counting() -> None:
    lines = iter(
        [
            f'CNTRL STORAGE 67108864 3 {CID} 32 32768 1000000 999000',
            'CNTRL ENTRY 1 0 D 0000000000008000 466163746f7279',
            'CNTRL ENTRY 2 1 F 0000000000008001 6269672e62696e',
            'CNTRL ENTRY 3 0 F 0 656d7074792066696c652e62696e',
            'CNTRL STORAGE END 3',
        ]
    )
    info = read_storage_report(lines.__next__)
    assert [e.path for e in info.entries] == [
        '/Factory',
        '/Factory/big.bin',
        '/empty file.bin',
    ]
    assert info.files_bytes == 32769 and info.estimated_file_allocation_bytes == 65536
    assert info.other_allocation_bytes == 32768000 - 65536
    assert info.directories[0].path == '/' and info.directories[0].files == 2
    assert info.directories[1].path == '/Factory' and info.directories[1].files == 1
    assert info.directories[1].size_bytes == 32769


def test_file_sizes_above_four_gib_are_preserved() -> None:
    lines = iter(
        [
            f'CNTRL STORAGE 134217728 3 {CID} 64 32768 2000000 1000',
            'CNTRL ENTRY 1 0 F 0000000100000001 626967',
            'CNTRL STORAGE END 1',
        ]
    )
    info = read_storage_report(lines.__next__)
    assert info.files_bytes == 2**32 + 1
    assert info.estimated_file_allocation_bytes == 2**32 + 32768


@pytest.mark.parametrize(
    'entry',
    [
        'CNTRL ENTRY 1 9 F 1 61',
        'CNTRL ENTRY 2 0 F 1 61',
        'CNTRL ENTRY 1 0 F 1 2e2e',
        'CNTRL ENTRY 1 0 F 1 612f62',
        'CNTRL STORAGE END 1',
        'CNTRL ERROR storage-list',
    ],
)
def test_invalid_or_incomplete_listing_is_not_reported_as_complete(entry: str) -> None:
    lines = iter([f'CNTRL STORAGE 67108864 3 {CID} 32 32768 1000000 999000', entry])
    with pytest.raises(ValueError):
        read_storage_report(lines.__next__)
