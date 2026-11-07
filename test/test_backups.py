"""Backup binaries stay authoritative and must match the current format."""

from pathlib import Path

import pytest

from cntrl.patch_switcher import backups, compiler


def test_backup_reader_preserves_exact_setup_bytes(tmp_path: Path) -> None:
    data = bytes.fromhex(
        (Path(__file__).parents[1] / 'firmware/test/show.golden').read_text()
    )
    folder = tmp_path / 'setups'
    folder.mkdir()
    (folder / 'Evening.bin').write_bytes(data)
    (tmp_path / 'songs.bin').write_bytes(b'unrelated library bytes')
    assert backups.read_setup_binaries(tmp_path) == {'Evening': data}


def test_backup_reader_rejects_an_old_document_format(tmp_path: Path) -> None:
    data = bytearray.fromhex(
        (Path(__file__).parents[1] / 'firmware/test/show.golden').read_text()
    )
    data[4:6] = (compiler.DOCUMENT_VERSION - 1).to_bytes(2, 'little')
    folder = tmp_path / 'setups'
    folder.mkdir()
    (folder / 'Evening.bin').write_bytes(data)
    with pytest.raises(ValueError, match='invalid setup header'):
        backups.read_setup_binaries(tmp_path)
