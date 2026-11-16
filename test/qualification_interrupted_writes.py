"""Explicit qualification: select this file to test every interrupted-write byte."""

from scripts import native_tests


def test_exhaustive_interrupted_write_recovery(
    native_programs: native_tests.NativePrograms,
) -> None:
    root = native_programs.source_root
    native_tests.run_native(
        [
            str(native_programs.build('test/test_switcher.cpp')),
            str(root / 'firmware/test/show.golden'),
            str(root / 'firmware/test/display.golden'),
            '--exhaustive',
            '--profile',
        ]
    )
