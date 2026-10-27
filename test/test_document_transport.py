"""Exercise the Python sender against the real C++ receiver through a PTY."""

import os
import pty
import subprocess
import sys
from pathlib import Path
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


def test_named_setup_lifecycle_against_cpp_receiver(peer: Path) -> None:
    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        port = Path(os.ttyname(slave))
        assert transport.device_status(port) == 'CNTRL READY 14 0 00000000'
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
            == f'CNTRL READY 14 {len(data)} {crc32(data):08x}'
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
        assert transport.device_status(port) == 'CNTRL READY 14 0 00000000'
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
    assert compiler.compile_show(exported, {'setups': []}, [], target) == data


def test_setup_cli_copy_download_and_delete(
    peer: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        port = os.ttyname(slave)

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
        assert 'Evening: copied and verified' in capsys.readouterr().out
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
            ['copy', f'Resolved={downloaded}', '--target', str(fixture / 'target.json')]
        )
        run(['del', 'Evening', 'Jazz', 'Resolved', '-y'])
        capsys.readouterr()
        run(['list'])
        assert capsys.readouterr().out == ''
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)
