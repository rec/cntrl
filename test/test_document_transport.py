"""Exercise the Python sender against the real C++ receiver through a PTY."""

import os
import pty
import subprocess
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.patch_switcher import transport


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


def test_upload_verifies_cpp_receiver_status(peer: Path) -> None:
    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        port = Path(os.ttyname(slave))
        assert transport.device_status(port) == 'CNTRL READY 5 0 00000000'
        result = transport.upload_document(port, data)
        assert result == f'CNTRL READY 5 {len(data)} {crc32(data):08x}'
        assert transport.device_status(port) == result
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
            transport.upload_document(port, bytes(data))
        assert transport.device_status(port) == 'CNTRL READY 5 0 00000000'
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


ROOT = Path(__file__).resolve().parents[1]
