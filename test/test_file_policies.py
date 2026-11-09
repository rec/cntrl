"""Portable file commit policies and listing work through filesystem primitives."""

from pathlib import Path

from cntrl.patch_switcher.storage_info import read_storage_report
from scripts import native_tests


def test_slot_and_asset_commits_and_directory_listing(tmp_path: Path) -> None:
    root = Path(__file__).parents[1]
    program = tmp_path / 'file-policies'
    card = tmp_path / 'card'
    card.mkdir()
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_file_policies.cpp'),
            '-o',
            str(program),
        ],
        check=True,
    )
    result = native_tests.run_native(
        [str(program), str(card)], capture_output=True, text=True, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert (card / 'name café.bin').read_bytes() == b'test'
    lines = iter(
        [
            'CNTRL STORAGE 34359738368 32768000000 - 32768',
            *result.stdout.splitlines(),
        ]
    )
    report = read_storage_report(lines.__next__)
    entries = {e.path: e for e in report.entries}
    assert entries['/cntrl/songs/0.bin'].size_bytes == 2
    assert entries['/cntrl/movies/0000abcd.bin'].size_bytes == 24
    assert entries['/name café.bin'].size_bytes == 4
    assert entries['/' + 'x' * 255].size_bytes == 4
