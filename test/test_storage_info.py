"""Decode storage byte counts, implementation details, and bounded listings."""

import pytest

from cntrl.patch_switcher.storage_info import (
    parse_storage_info,
    read_storage_report,
)


def test_storage_can_report_internal_flash_without_device_identity() -> None:
    info = parse_storage_info('CNTRL STORAGE 34359738368 32768000000 - 32768')
    assert info.capacity_bytes == 32 * 1024**3
    assert info.usable_bytes == 32768000000
    assert info.allocation_unit_bytes == 32768
    assert info.free_bytes is None and info.used_bytes is None
    assert info.details == {}


def test_scan_reports_used_and_free_bytes() -> None:
    info = parse_storage_info('CNTRL STORAGE 68719476736 65536000000 32768000 32768')
    assert info.free_bytes == 32768 * 1000
    assert info.used_bytes == 32768 * 1999000


def test_optional_details_preserve_arbitrary_storage_identity() -> None:
    lines = iter(
        [
            'CNTRL STORAGE 1048576 1000000 - 16',
            'CNTRL DETAIL 66696c6573797374656d 4c6974746c654653',
            'CNTRL DETAIL 70726f64756374 496e7465726e616c20666c617368',
            'CNTRL STORAGE END 0',
        ]
    )
    info = read_storage_report(lines.__next__)
    assert info.details == {'filesystem': 'LittleFS', 'product': 'Internal flash'}
    assert info.entries == [] and info.free_bytes is None


@pytest.mark.parametrize(
    'response',
    [
        'CNTRL ERROR storage-unavailable',
        'CNTRL STORAGE 100 101 - 1',
        'CNTRL STORAGE 100 100 101 1',
        'CNTRL STORAGE -1 0 - 1',
        'CNTRL STORAGE 100 100 - 0',
    ],
)
def test_invalid_storage_responses_are_errors(response: str) -> None:
    with pytest.raises(ValueError):
        parse_storage_info(response)


def test_listing_reconstructs_paths_and_totals_without_double_counting() -> None:
    lines = iter(
        [
            'CNTRL STORAGE 34359738368 32768000000 32735232000 32768',
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
            'CNTRL STORAGE 68719476736 65536000000 32768000 32768',
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
    lines = iter(['CNTRL STORAGE 34359738368 32768000000 32735232000 32768', entry])
    with pytest.raises(ValueError):
        read_storage_report(lines.__next__)
