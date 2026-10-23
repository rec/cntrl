"""Prepared frame geometry and a real Python/C++ transfer over a pseudo-terminal."""

import os
import pty
import struct
import subprocess
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.maker.morningstar.mc.splash import asset, cli


def test_mac_prepares_outer_and_mirrored_centre_rows() -> None:
    row = b''.join(struct.pack('>H', x) for x in range(asset.WIDTH))
    data = asset.encode_frames(row * asset.HEIGHT * 2, [100, 200])
    info = asset.inspect_asset(data)
    assert info.frames == 2 and info.duration_ms == 300
    assert info.size == asset.HEADER.size + 2 * asset.FRAME_BYTES
    for frame, duration in enumerate([100, 200]):
        offset = asset.HEADER.size + frame * asset.FRAME_BYTES
        assert struct.unpack_from('<I', data, offset)[0] == duration
        for y in range(asset.HEIGHT):
            start = offset + 4 + y * asset.WIDTH * 4
            assert data[start : start + asset.WIDTH * 2] == row
            centre = data[start + asset.WIDTH * 2 : start + asset.WIDTH * 4]
            assert list(struct.unpack('>160H', centre)) == list(reversed(range(160)))


@pytest.mark.parametrize('durations', [[], [0], [-1], [True], [0x80000000]])
def test_invalid_durations_are_rejected(durations: list[int]) -> None:
    with pytest.raises(ValueError):
        asset.encode_frames(
            bytes(len(durations) * asset.WIDTH * asset.HEIGHT * 2), durations
        )


def test_malformed_asset_is_rejected_before_opening_serial() -> None:
    with pytest.raises(ValueError, match='header'):
        cli.upload_asset(Path('/not-a-device'), b'bad')
    data = asset.encode_frames(bytes(asset.WIDTH * asset.HEIGHT * 2), [100])
    with pytest.raises(ValueError, match='size'):
        asset.inspect_asset(data[:-1])
    zero = bytearray(data)
    struct.pack_into('<I', zero, asset.HEADER.size, 0)
    with pytest.raises(ValueError, match='duration'):
        asset.inspect_asset(bytes(zero))


@pytest.fixture(scope='module')
def peer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    executable = tmp_path_factory.mktemp('splash-peer') / 'peer'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            f'-I{ROOT.parents[4] / "firmware/include"}',
            str(ROOT / 'test/peer.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    return executable


@pytest.mark.parametrize('frames', [1, 16])
def test_python_upload_is_verified_by_real_cpp_receiver(
    peer: Path, frames: int
) -> None:
    data = asset.encode_frames(
        bytes(asset.WIDTH * asset.HEIGHT * 2 * frames), [100] * frames
    )
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer)], stdin=master, stdout=master)
    try:
        result = cli.upload_asset(Path(os.ttyname(slave)), data)
        assert result == f'CNTRL SPLASH READY {len(data)} {crc32(data):08x}'
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


ROOT = Path(__file__).resolve().parents[1]
