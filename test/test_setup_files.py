"""File-backed setup history and append/patch behavior through storage services."""

from pathlib import Path

from scripts import native_tests


def test_setup_history_preserves_commits_and_reserved_revisions(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / 'setup-files'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-pedantic',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_setup_files.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    data = tmp_path / 'data'
    data.mkdir()
    native_tests.run_native([str(executable), str(data)], check=True)
