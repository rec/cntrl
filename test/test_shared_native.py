"""Collect the remaining shared native programs in the normal pytest run."""

import pytest

from scripts import native_tests


@pytest.mark.parametrize('source,arguments', native_tests.cases().items())
def test_shared_native_program(
    native_programs: native_tests.NativePrograms, source: str, arguments: list[str]
) -> None:
    native_tests.run_native(
        [
            str(native_programs.build(source)),
            *(str(native_programs.source_root / 'firmware' / a) for a in arguments),
        ]
    )
