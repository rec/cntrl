"""Configure repository tests and isolate automatic user configuration."""

from pathlib import Path

import pytest

from cntrl.patch_chooser import global_config


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        '--require-morningstar-sdk',
        action='store_true',
        help='Fail if the pinned Teensy SDK is missing for Morningstar font checks.',
    )


@pytest.fixture(autouse=True)
def isolated_global_config(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def path() -> Path:
        return tmp_path / 'user-config' / 'global.toml'

    monkeypatch.setattr(global_config, 'user_path', path)
