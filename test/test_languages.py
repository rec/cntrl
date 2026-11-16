"""Language packs activate transactionally through the native runtime."""

import struct
import subprocess
from pathlib import Path
from zlib import crc32

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
    legacy = bytearray((packs / 'fr.bin').read_bytes())
    struct.pack_into('<H', legacy, 4, 1)
    struct.pack_into('<I', legacy, 12, 0)
    struct.pack_into('<I', legacy, 12, crc32(legacy))
    legacy_path = tmp_path / 'legacy-fr.bin'
    legacy_path.write_bytes(legacy)
    subprocess.run(
        [
            str(executable),
            str(tmp_path / 'device'),
            str(packs / 'fr.bin'),
            str(packs / 'en.bin'),
            str(root / 'cntrl/fonts/bitmap_samples/TamzenBold7x13.cfont'),
            str(legacy_path),
        ],
        check=True,
    )
