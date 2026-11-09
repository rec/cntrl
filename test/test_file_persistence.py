"""Exercise file-backed recording and calibration policy through a neutral API."""

from pathlib import Path

from scripts import native_tests


def test_recordings_and_calibrations_survive_file_reload(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / 'persistence'
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
            str(root / 'firmware/test/test_file_persistence.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    data = tmp_path / 'data'
    data.mkdir()
    native_tests.run_native([str(executable), str(data)], check=True)
