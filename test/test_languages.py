"""Language packs activate transactionally through the native runtime."""

import subprocess
from pathlib import Path

from scripts import native_tests


def test_language_switch_preserves_the_old_ui_until_assets_are_ready(
    tmp_path: Path,
) -> None:
    root = Path(__file__).parents[1]
    executable = tmp_path / 'languages'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_languages.cpp'),
            '-o',
            str(executable),
        ]
    )
    packs = root / 'cntrl/localization/packs'
    subprocess.run(
        [
            str(executable),
            str(tmp_path / 'device'),
            str(packs / 'fr.bin'),
            str(packs / 'en.bin'),
        ],
        check=True,
    )
