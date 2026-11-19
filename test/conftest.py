"""Shared executables are compiled once per pytest session, including wrappers."""

import struct
from pathlib import Path
from zlib import crc32

import pytest

from scripts.native_tests import NativePrograms


@pytest.fixture(scope='session')
def native_programs(tmp_path_factory: pytest.TempPathFactory) -> NativePrograms:
    return NativePrograms(
        Path(__file__).parents[1], tmp_path_factory.mktemp('native-programs')
    )


@pytest.fixture(scope='session')
def legacy_setups() -> dict[int, bytes]:
    """Keep old format coverage independent of removed utility pages.

    Original historical files stay unchanged for the removal/rejection tests.
    These variants replace those utility slots with additional Status pages.
    """
    result = {}
    root = Path(__file__).parents[1] / 'firmware/test'
    for version in (36, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47):
        data = bytearray(bytes.fromhex((root / f'setup{version}.golden').read_text()))
        page = data.index(b'CPAG')
        count, windows, layout_version = data[page + 4 : page + 7]
        start = page + (8 if layout_version == 1 else 12) + windows * 32
        for index in range(count):
            record = start + index * (34 + windows)
            if (kind := data[record + 32]) in (2, 7, 8, 9):
                data[record : record + 32] = f'status-{kind}'.encode().ljust(32, b'\0')
                data[record + 32] = 1
        struct.pack_into('<I', data, 20, crc32(data[28:]))
        result[version] = bytes(data)
    return result
