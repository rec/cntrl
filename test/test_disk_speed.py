"""Disk throughput exercises the production C++ transfer and real scratch files."""

import sys
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import TYPE_CHECKING

import pytest
import tyro
from native_peer import NativePeer

from cntrl.patch_switcher import cli, devices, disk_speed, transport
from scripts import native_tests

if TYPE_CHECKING:
    from pytest_regressions.file_regression import FileRegressionFixture


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
    ticks = iter([0, 12_500_000, 12_500_000, 37_750_000])
    monkeypatch.setattr(disk_speed, 'perf_counter_ns', lambda: next(ticks))
    monkeypatch.setattr(sys.stderr, 'isatty', lambda: True)
    with device(peer, tmp_path) as port:
        monkeypatch.setattr(
            devices, 'connected', lambda: [devices.Device(port=port, name='cntrl test')]
        )
        monkeypatch.setattr(sys, 'argv', ['cntrl', 'status', 'disk-speed'])
        cli.main()
    output = capsys.readouterr()
    upload, download, total = output.out.splitlines()
    assert upload == 'Upload: 80,000,000 bytes/second'
    assert download == 'Download: 39,603,960 bytes/second'
    assert total == 'Round trip: 52,980,132 bytes/second'
    assert 'Upload:' in output.err and 'Download:' in output.err
    assert '100%' in output.err and '1.00M/1.00M' in output.err
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
            client.send_data(transport.Kind.disk, b'\x01')
            assert (tmp_path / 'cntrl/.disk-speed.tmp').exists()
        assert not (tmp_path / 'cntrl/.disk-speed.tmp').exists()


@pytest.mark.parametrize(
    'command', ['CNTRL DISK INVALID', 'CNTRL DISK END', 'CNTRL DISK READ 1000000']
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
    assert isinstance(automatic.command, cli.DiskSpeed) and automatic.port is None
    assert tyro.cli(cli.Status, args=[]).port is None
    command = tyro.cli(cli.Status, args=['disk-speed', '--port', '/unused'])
    assert isinstance(command.command, cli.DiskSpeed) and command.port == Path(
        '/unused'
    )
    assert tyro.cli(cli.Status, args=['--port', '/unused']).command is None


def test_disk_speed_help_describes_the_benchmark_and_its_options(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    file_regression: 'FileRegressionFixture',
) -> None:
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'status', 'disk-speed', '-h'])
    with pytest.raises(SystemExit) as caught:
        cli.main()
    assert caught.value.code == 0
    help_text = capsys.readouterr().out
    assert 'status disk-speed' in help_text
    assert '1,000,000-byte' in help_text
    assert 'bytes/second' in help_text
    assert '--port' in help_text
    file_regression.check(help_text)
