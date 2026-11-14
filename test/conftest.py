"""Shared executables are compiled once per pytest session, including wrappers."""

from pathlib import Path

import pytest

from scripts.native_tests import NativePrograms


@pytest.fixture(scope='session')
def native_programs(tmp_path_factory: pytest.TempPathFactory) -> NativePrograms:
    return NativePrograms(
        Path(__file__).parents[1], tmp_path_factory.mktemp('native-programs')
    )
