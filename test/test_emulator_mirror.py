"""Mirroring preserves the stored session instead of applying current defaults."""

from hashlib import sha256
from pathlib import Path

import pytest
import tomlkit
import tyro

from cntrl.emulator import mirror
from cntrl.emulator.cli import Emulate
from cntrl.patch_switcher import storage_info, transport


class CapturedPedal(transport.SetupClient):
    def __init__(self, files: dict[str, bytes], changing: bool = False) -> None:
        self.files = files
        self.changing = changing
        self.reads: dict[str, int] = {}

    def current(self) -> str:
        return 'stored-setup'

    def request(self, command: str) -> str:
        assert command == 'CNTRL STATUS'
        return 'CNTRL READY persistent_root=305419896'

    def download_bundle(self) -> bytes:
        return b'exact compiled bundle'

    def request_data(self, command: str) -> bytes:
        assert command == 'CNTRL STATUS SNAPSHOT'
        return b'CSS1' + bytes(107)

    def download_state(self, path: str) -> bytes:
        self.reads[path] = self.reads.get(path, 0) + 1
        if self.changing and self.reads[path] > 1:
            return b'changed journal'
        return self.files[path]


def test_mirror_invocation_uses_the_connected_controller() -> None:
    command = tyro.cli(Emulate, args=['--port', '/dev/test-pedal'])
    assert command.port == Path('/dev/test-pedal')
    assert command.setup is None
    assert command.controller is None


@pytest.mark.parametrize(
    'arguments',
    [
        [],
        ['setup.toml', '--port', '/dev/test-pedal'],
        ['--port', '/dev/test-pedal', '--layout', 'layout.toml'],
    ],
)
def test_mirror_requires_one_session_source(arguments: list[str]) -> None:
    with pytest.raises((ValueError, SystemExit)):
        tyro.cli(Emulate, args=arguments)


@pytest.mark.parametrize('changing', [False, True])
def test_capture_preserves_active_journals_and_exact_asset_set(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, changing: bool
) -> None:
    files = {
        '/cntrl/generations/12345678/setups/bundle/0.bin': b'active bundle journal',
        '/cntrl/generations/12345678/selection/1.bin': b'stored selection',
        '/cntrl/generations/12345678/global/0.bin': b'French preference',
        '/cntrl/generations/12345678/languages/fr.bin': b'French pack',
        '/cntrl/generations/11111111/languages/de.bin': b'inactive German pack',
        '/cntrl/languages/es.bin': b'stale Spanish pack',
        '/cntrl/fonts/01020304.cfont': b'exact font bytes',
        '/cntrl/movies/12345678.bin': b'exact movie bytes',
    }
    report = storage_info.StorageInfo(
        capacity_bytes=100000,
        allocation_unit_bytes=512,
        usable_bytes=100000,
        free_bytes=90000,
        used_bytes=10000,
        entries=[
            storage_info.CardEntry(
                path=p,
                directory=False,
                size_bytes=len(d),
                estimated_allocation_bytes=512,
            )
            for p, d in files.items()
        ],
    )
    monkeypatch.setattr(storage_info, 'read_storage_report', lambda read: report)
    client = CapturedPedal(files, changing)
    if changing:
        with pytest.raises(ValueError, match='changed during mirror capture'):
            mirror.capture(client, tmp_path, 'CNTRL READY persistent_root=305419896')
        assert not (tmp_path / 'mirror.toml').exists()
        return
    setup = mirror.capture(client, tmp_path, 'CNTRL READY persistent_root=305419896')
    assert setup.read_bytes() == b'exact compiled bundle'
    assert (tmp_path / 'setup-source.sha256').read_text() == sha256(
        setup.read_bytes()
    ).hexdigest()
    actual = {
        p.relative_to(tmp_path / 'cntrl').as_posix(): p.read_bytes()
        for p in (tmp_path / 'cntrl').rglob('*')
        if p.is_file()
    }
    assert actual == {
        'setups/bundle/0.bin': b'active bundle journal',
        'selection/1.bin': b'stored selection',
        'global/0.bin': b'French preference',
        'languages/fr.bin': b'French pack',
        'fonts/01020304.cfont': b'exact font bytes',
        'movies/12345678.bin': b'exact movie bytes',
    }
    manifest = tomlkit.loads((tmp_path / 'mirror.toml').read_text())
    assert manifest['selected'] == 'stored-setup'
    assert (tmp_path / 'status-snapshot.bin').read_bytes() == b'CSS1' + bytes(107)
