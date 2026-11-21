"""Mirroring preserves the stored session instead of applying current defaults."""

from contextlib import nullcontext
from hashlib import sha256
from pathlib import Path

import pytest
import tomlkit
import tyro

from cntrl.emulator import mirror
from cntrl.emulator.cli import Emulate
from cntrl.patch_chooser import binary_format, storage_info, transport


class CapturedPedal(transport.SetupClient):
    def __init__(self, files: dict[str, bytes], changed_file: str) -> None:
        self.files = files
        self.changed_file = changed_file
        self.reads: dict[str, int] = {}

    def current(self) -> str:
        return 'stored-setup'

    def request(self, command: str) -> str:
        assert command == 'CNTRL STATUS'
        return (
            'CNTRL READY model=mc6-pro persistent_root=305419896 '
            f'persistent_format={binary_format.DEVELOPMENT_FORMAT}'
        )

    def download_bundle(self) -> bytes:
        return b'exact compiled bundle'

    def request_data(self, command: str) -> bytes:
        assert command == 'CNTRL STATUS SNAPSHOT'
        return b'CSS1' + bytes(107)

    def download_state(self, path: str) -> bytes:
        self.reads[path] = self.reads.get(path, 0) + 1
        data = self.files[path]
        if path == self.changed_file and self.reads[path] > 1:
            return bytes([data[0] ^ 1]) + data[1:]
        return data


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


@pytest.mark.parametrize('change', ['', 'asset', 'journal', 'new-slot'])
def test_capture_preserves_active_journals_and_exact_asset_set(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, change: str
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
    second_report = report
    if change == 'new-slot':
        new_slot = report.entries[0].model_copy(
            update={'path': '/cntrl/generations/12345678/setups/bundle/1.bin'}
        )
        second_report = report.model_copy(
            update={'entries': [*report.entries, new_slot]}
        )
    reports = iter([report, second_report])
    monkeypatch.setattr(storage_info, 'read_storage_report', lambda read: next(reports))
    changed_file = {
        'asset': '/cntrl/fonts/01020304.cfont',
        'journal': '/cntrl/generations/12345678/global/0.bin',
    }.get(change, '')
    client = CapturedPedal(files, changed_file)
    monkeypatch.setattr(transport, 'SerialConnection', lambda *a, **k: nullcontext())
    monkeypatch.setattr(transport, 'SetupClient', lambda _: client)
    if change:
        with pytest.raises(ValueError, match='changed during mirror capture'):
            mirror.download(Path('/dev/test-pedal'), tmp_path, None)
        assert not list(tmp_path.iterdir())
        return
    model, setup, capture = mirror.download(Path('/dev/test-pedal'), tmp_path, None)
    assert model == 'mc6-pro'
    assert list(tmp_path.iterdir()) == [capture]
    assert setup.read_bytes() == b'exact compiled bundle'
    assert (capture / 'setup-source.sha256').read_text() == sha256(
        setup.read_bytes()
    ).hexdigest()
    actual = {
        p.relative_to(capture / 'cntrl').as_posix(): p.read_bytes()
        for p in (capture / 'cntrl').rglob('*')
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
    manifest = tomlkit.loads((capture / 'mirror.toml').read_text())
    assert manifest['selected'] == 'stored-setup'
    assert (capture / 'status-snapshot.bin').read_bytes() == b'CSS1' + bytes(107)
