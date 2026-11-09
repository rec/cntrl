"""Qualify the maintained native adapter tests with the selected host compiler."""

from pathlib import Path

from cntrl.maker.morningstar.mc import native_tests


def test_adapter_native_suite(tmp_path: Path) -> None:
    native_tests.run(native_tests.Tests(output=tmp_path))
