"""Keep automatic user configuration outside every test's environment."""

from pathlib import Path

import pytest

from cntrl.patch_chooser import global_config


@pytest.fixture(autouse=True)
def isolated_global_config(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def path() -> Path:
        return tmp_path / 'user-config' / 'global.toml'

    monkeypatch.setattr(global_config, 'user_path', path)
