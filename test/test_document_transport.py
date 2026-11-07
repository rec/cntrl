"""Exercise the Python sender against the real C++ receiver through a PTY."""

import os
import pty
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path
from tempfile import TemporaryDirectory
from zlib import crc32

import pytest

from cntrl.patch_switcher import cli, compiler, models, resolved, transport


@pytest.fixture(scope='module')
def peer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    executable = tmp_path_factory.mktemp('document-peer') / 'peer'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            f'-I{ROOT / "firmware/include"}',
            str(ROOT / 'firmware/test/document_server.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    return executable


@pytest.fixture(scope='module')
def bundle_peer() -> Iterator[Path]:
    with TemporaryDirectory(prefix='cntrl-bundle-peer-') as directory:
        executable = Path(directory) / 'peer'
        subprocess.run(
            [
                'c++',
                '-std=c++17',
                '-Wall',
                '-Wextra',
                '-Werror',
                f'-I{ROOT / "firmware/include"}',
                str(ROOT / 'firmware/test/bundle_server.cpp'),
                '-o',
                str(executable),
            ],
            check=True,
        )
        yield executable


def test_named_setup_lifecycle_against_cpp_receiver(peer: Path) -> None:
    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        port = Path(os.ttyname(slave))
        assert (
            transport.device_status(port)
            == f'CNTRL READY {compiler.DOCUMENT_VERSION} 0 00000000'
        )
        with transport.SerialConnection(port) as connection:
            client = transport.SetupClient(connection)
            assert client.catalogue() == []
            assert client.current() == ''
            with pytest.raises(ValueError, match='rotation-failed'):
                client.ok('CNTRL SETUP FORWARD')
            client.ok('CNTRL TIME 1792843200')
            client.copy('Jazz + A=B', data, False)
            client.copy('Evening', data, False)
            assert client.current() == ''
            client.ok('CNTRL SETUP FINISH')
            assert client.current() == 'Evening'
            assert [e.name for e in client.catalogue()] == ['Evening', 'Jazz + A=B']
            assert all(e.modified == 1792843200 for e in client.catalogue())
            with pytest.raises(ValueError, match='setup-exists'):
                client.copy('Evening', data, False)
            with pytest.raises(ValueError, match='setup-exists'):
                client.copy('evening', data, True)
            client.copy('Evening', data, True)
            client.ok('CNTRL SETUP FORWARD')
            assert client.current() == 'Jazz + A=B'
            client.ok('CNTRL SETUP FORWARD')
            assert client.current() == 'Evening'
            client.ok('CNTRL SETUP BACKWARD')
            assert client.current() == 'Jazz + A=B'
            assert client.download('Jazz + A=B') == data
            client.delete(['Jazz + A=B', 'Evening'])
            assert client.current() == '' and client.catalogue() == []
            client.copy('Again', data, False)
            client.ok('CNTRL SETUP FINISH')
            assert client.current() == 'Again'
        assert (
            transport.device_status(port)
            == f'CNTRL READY {compiler.DOCUMENT_VERSION} {len(data)} {crc32(data):08x}'
        )
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_cpp_receiver_rejects_corrupt_document(peer: Path) -> None:
    data = bytearray.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    data[-1] ^= 1
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        port = Path(os.ttyname(slave))
        with pytest.raises(ValueError, match='invalid-document'):
            with transport.SerialConnection(port) as connection:
                transport.SetupClient(connection).copy('Bad', bytes(data), False)
        assert (
            transport.device_status(port)
            == f'CNTRL READY {compiler.DOCUMENT_VERSION} 0 00000000'
        )
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_large_setup_uses_the_shared_document_budget(peer: Path) -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )

    def setup(size: int, value: int = 1) -> bytes:
        return compiler.compile_show(
            {
                'patches': {
                    str(i): {'data': [value] * size, 'output': 'din'} for i in range(2)
                }
            },
            {},
            [],
            target,
        )

    data = setup(40000)
    too_large_alongside_active = setup(60000)
    assert len(data) > 65536
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
            client = transport.SetupClient(connection)
            client.copy('Large', data, False)
            client.ok('CNTRL SETUP FINISH')
            assert client.download('Large') == data
            with pytest.raises(ValueError, match='document-memory'):
                client.copy('Larger', too_large_alongside_active, False)
            with pytest.raises(ValueError, match='history-failed'):
                client.copy('Large', setup(40000, 2), True)
            assert client.current() == 'Large'
            assert client.download('Large') == data
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


ROOT = Path(__file__).resolve().parents[1]


def test_resolved_export_recompiles_without_original_sources() -> None:
    import tomlkit

    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {'profiles': []}, [], target) == data


def test_setup_cli_copy_download_and_delete(
    bundle_peer: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    master, slave = pty.openpty()
    process = subprocess.Popen([str(bundle_peer)], stdin=master, stdout=master)
    try:
        port = os.ttyname(slave)
        with transport.SerialConnection(Path(port)) as connection:
            assert transport.SetupClient(connection).memory() == models.TargetMemory(
                capacity=400000, runtime=240000
            )

        def run(arguments: list[str]) -> None:
            monkeypatch.setattr(
                sys, 'argv', ['cntrl', 'setup', *arguments, '--port', port]
            )
            cli.main()

        fixture = ROOT / 'firmware/test'
        run(
            [
                'copy',
                f'Evening={fixture / "show.json"}',
                f'Jazz={fixture / "show.json"}',
                '--target',
                str(fixture / 'target.json'),
                '--definitions',
                str(fixture / 'definitions.json'),
            ]
        )
        assert 'Evening:' in capsys.readouterr().out
        run(['set'])
        assert capsys.readouterr().out.strip() == 'Evening'
        run(['rotate', '-b'])
        run(['set'])
        assert capsys.readouterr().out.strip() == 'Jazz'
        run(['download', 'Evening', '--directory', str(tmp_path)])
        downloaded = tmp_path / 'Evening.toml'
        assert downloaded.exists()
        with pytest.raises(SystemExit, match='--overwrite'):
            run(['download', 'Evening', '--directory', str(tmp_path)])
        run(['download', 'Evening', '--directory', str(tmp_path), '-o'])
        run(
            [
                'copy',
                f'Resolved={downloaded}',
                '--target',
                str(fixture / 'target.json'),
                '--overwrite',
            ]
        )
        run(['set', 'Resolved'])
        run(['del', 'Resolved', '-y'])
        capsys.readouterr()
        run(['list'])
        assert capsys.readouterr().out == ''
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_revision_commands_against_cpp_receiver(peer: Path) -> None:
    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
            client = transport.SetupClient(connection)
            client.copy('History', data, False)
            name = b'History'.hex()
            client.ok(f'CNTRL HISTORY SAVE {name} 0 {b"Rehearsal".hex()}')
            response = client.request(f'CNTRL HISTORY LIST {name} {2**64 - 1}')
            assert response.split()[2] == '1'
            assert client.download('History', 1) == data
            client.ok(f'CNTRL HISTORY RESTORE {name} 1')
            client.delete(['History'])
            assert client.download('History', 1) == data
            client.ok(f'CNTRL HISTORY RESTORE {name} 1')
            assert client.download('History') == data
            client.ok(f'CNTRL HISTORY DELETE {name} 0')
            assert (
                client.request(f'CNTRL HISTORY LIST {name} {2**64 - 1}') == 'CNTRL END'
            )
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_progress_reply_is_not_returned_as_the_command_result() -> None:
    connection = transport.SerialConnection(Path('/unused'))
    connection.received.extend(b'CNTRL WAIT\nCNTRL WAIT\nCNTRL OK\n')
    assert connection.line() == 'CNTRL OK'


def test_reconnect_does_not_repeat_rotation_or_transfer_bytes(
    peer: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        with transport.SerialConnection(
            Path(os.ttyname(slave)), timeout=1
        ) as connection:
            client = transport.SetupClient(connection)
            write_frame = connection.write_frame
            read_frame = connection.read_frame
            writes = 0
            losses = 0

            def interrupted_write(frame: bytes) -> None:
                nonlocal writes
                writes += 1
                if writes == 3:
                    write_frame(frame[: len(frame) // 2])
                    raise OSError('injected partial USB write')
                write_frame(frame)

            def lost_reply() -> str:
                nonlocal losses
                result = read_frame()
                if writes == 10 and losses == 0:
                    losses += 1
                    raise OSError('injected lost data acknowledgement')
                return result

            monkeypatch.setattr(connection, 'write_frame', interrupted_write)
            monkeypatch.setattr(connection, 'read_frame', lost_reply)
            client.copy('A', data, False)
            assert losses == 1
            client.copy('B', data, False)
            client.select('A')
            losses = 0

            def lost_completion() -> str:
                nonlocal losses
                result = read_frame()
                if result.endswith(b'CNTRL OK\n'.hex()) and losses < 3:
                    losses += 1
                    raise OSError('injected lost command completion')
                return result

            monkeypatch.setattr(connection, 'read_frame', lost_completion)
            monkeypatch.setattr(transport.time, 'sleep', lambda _: None)
            client.ok('CNTRL SETUP FORWARD')
            assert losses == 3
            assert client.current() == 'B'
            losses = 0
            client.ok(f'CNTRL HISTORY SAVE {b"A".hex()} 0 -')
            assert losses == 3
            revision = client.request(f'CNTRL HISTORY LIST {b"A".hex()} {2**64 - 1}')
            assert revision.split()[2] == '1'
            assert client.request(f'CNTRL HISTORY LIST {b"A".hex()} 1') == 'CNTRL END'
            assert client.download('A') == data
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_usb_wait_backoff_keeps_retrying_until_control_c(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    connection = transport.SerialConnection(Path('/unplugged'))
    attempts = 0
    seconds = 0

    def unplugged() -> None:
        nonlocal attempts
        attempts += 1
        raise FileNotFoundError('USB unplugged')

    def wait(duration: float) -> None:
        nonlocal seconds
        seconds += int(duration)
        if seconds == 15:
            raise KeyboardInterrupt

    monkeypatch.setattr(connection, 'connect', unplugged)
    monkeypatch.setattr(transport.time, 'sleep', wait)
    with pytest.raises(KeyboardInterrupt):
        connection.__enter__()
    assert attempts == 6
    assert capsys.readouterr().err.splitlines() == [
        f'Waiting {n} seconds for USB (hit control-C to exit)' for n in (1, 2, 4, 8)
    ]


def test_midi_file_operations_survive_cpp_storage(peer: Path) -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    data = compiler.compile_show(
        {
            'patch_cycles': {
                'Files': [
                    {
                        'operation': 'play-midi-file',
                        'filename': '255-3.mid',
                        'outputs': ['din', 'trs'],
                        'speed': 1.5,
                        'reverse': True,
                    },
                    {'operation': 'record-midi-file', 'channels': [1, 16]},
                ]
            }
        },
        {},
        [],
        target,
    )
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
            client = transport.SetupClient(connection)
            client.copy('Files', data, False)
            assert client.download('Files') == data
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_history_import_preserves_metadata_and_refuses_conflicting_retries(
    bundle_peer: Path,
) -> None:
    from cntrl.patch_switcher import history

    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    data = compiler.compile_show({}, {}, [], target)
    revision = history.Revision(
        number=42, modified=1234567890, length=len(data), label='Before soundcheck'
    )
    master, slave = pty.openpty()
    process = subprocess.Popen([str(bundle_peer)], stdin=master, stdout=master)
    try:
        with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
            client = transport.SetupClient(connection)
            expected = history.import_revision(client, 'Evening', revision, data)
            assert history.revisions(client, 'Evening') == [revision]
            assert (
                history.import_revision(client, 'Evening', revision, data) == expected
            )
            with pytest.raises(ValueError, match='history-import-conflict'):
                history.import_revision(
                    client,
                    'Evening',
                    revision.model_copy(update={'label': 'Conflicting'}),
                    data,
                )
            assert client.download('Evening', 42) == expected
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)
