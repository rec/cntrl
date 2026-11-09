"""Disk timings exercise the production C++ transfer and real scratch files."""

import sys
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

import pytest
import tyro
from native_peer import NativePeer

from cntrl.patch_switcher import cli, devices, disk_speed, transport
from scripts import native_tests


@pytest.fixture(scope='module')
def peer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    root = Path(__file__).parents[1]
    executable = tmp_path_factory.mktemp('disk-speed-peer') / 'peer'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            '-I' + str(root / 'firmware/include'),
            str(root / 'firmware/test/disk_speed_server.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    return executable


@contextmanager
def device(peer: Path, directory: Path, failure: str = '') -> Iterator[Path]:
    with NativePeer([str(peer), str(directory), failure]) as port:
        yield Path(port)


def test_round_trip_reports_separate_times_and_removes_scratch_file(
    peer: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    canary = tmp_path / 'user-file'
    canary.write_bytes(b'keep this file')
    (tmp_path / 'cntrl').mkdir()
    (tmp_path / 'cntrl/.disk-speed.tmp').write_bytes(b'old interrupted test')
    ticks = iter([0, 12_500_000, 37_750_000])
    monkeypatch.setattr(disk_speed, 'perf_counter_ns', lambda: next(ticks))
    with device(peer, tmp_path) as port:
        monkeypatch.setattr(
            devices, 'connected', lambda: [devices.Device(port=port, name='cntrl test')]
        )
        monkeypatch.setattr(sys, 'argv', ['cntrl', 'status', 'disk-speed'])
        cli.main()
    upload, download, total = capsys.readouterr().out.splitlines()
    assert upload == 'Upload: 12.500 ms'
    assert download == 'Download: 25.250 ms'
    assert total == 'Total: 37.750 ms'
    assert canary.read_bytes() == b'keep this file'
    assert list((tmp_path / 'cntrl').iterdir()) == []


@pytest.mark.parametrize('failure', ['write', 'sync', 'read', 'corrupt'])
def test_failed_round_trip_is_reported_and_scratch_file_is_removed(
    peer: Path, tmp_path: Path, failure: str
) -> None:
    expected = 'read-back verification' if failure == 'corrupt' else 'disk-' + failure
    with device(peer, tmp_path, failure) as port:
        with pytest.raises(ValueError, match=expected):
            disk_speed.measure(port)
    assert list((tmp_path / 'cntrl').iterdir()) == []


def test_session_cancellation_removes_an_unfinished_upload(
    peer: Path, tmp_path: Path
) -> None:
    with device(peer, tmp_path) as port:
        with transport.SerialConnection(port) as connection:
            client = transport.SetupClient(connection)
            client.ok('CNTRL DISK BEGIN 0')
            client.ok('CNTRL DISK DATA 01')
            assert (tmp_path / 'cntrl/.disk-speed.tmp').exists()
        assert not (tmp_path / 'cntrl/.disk-speed.tmp').exists()


@pytest.mark.parametrize(
    'command', ['CNTRL DISK DATA zz', 'CNTRL DISK END', 'CNTRL DISK READ 1000000']
)
def test_invalid_transfer_is_rejected_and_removed(
    peer: Path, tmp_path: Path, command: str
) -> None:
    with device(peer, tmp_path) as port:
        with transport.SerialConnection(port) as connection:
            client = transport.SetupClient(connection)
            client.ok('CNTRL DISK BEGIN 0')
            with pytest.raises(ValueError, match='CNTRL ERROR disk-'):
                client.request(command)
            assert not (tmp_path / 'cntrl/.disk-speed.tmp').exists()


def test_disk_speed_status_command_parses() -> None:
    automatic = tyro.cli(cli.Status, args=['disk-speed'])
    assert automatic.action == 'disk-speed' and automatic.port is None
    assert tyro.cli(cli.Status, args=[]).port is None
    command = tyro.cli(cli.Status, args=['disk-speed', '--port', '/unused'])
    assert command.action == 'disk-speed' and command.port == Path('/unused')
    assert tyro.cli(cli.Status, args=['--port', '/unused']).action is None
