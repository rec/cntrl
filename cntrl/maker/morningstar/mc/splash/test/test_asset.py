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


def test_gif_is_enlarged_and_centre_cropped(tmp_path: Path) -> None:
    source = tmp_path / 'border.gif'
    subprocess.run(
        [
            'ffmpeg',
            '-v',
            'error',
            '-f',
            'lavfi',
            '-i',
            'color=blue:size=160x128,'
            'drawbox=x=0:y=0:w=iw:h=ih:color=red:t=4,'
            'drawbox=x=64:y=52:w=32:h=24:color=lime:t=fill',
            '-frames:v',
            '1',
            str(source),
        ],
        capture_output=True,
        check=True,
    )
    data = asset.prepare_gif(source)
    assert asset.inspect_asset(data).frames == 1
    rows = [
        struct.unpack_from('>160H', data, asset.HEADER.size + 4 + y * 640)
        for y in range(asset.HEIGHT)
    ]
    # Zoom removes the border on every edge, while enlarging the centre marker.
    edges = list(rows[0] + rows[-1]) + [p for r in rows for p in (r[0], r[-1])]
    # GIF palette quantization and RGB565 rounding need not preserve exact RGB.
    assert all(p >> 11 < 3 and p & 0x07E0 < 0x100 and p & 0x1F > 24 for p in edges)
    assert sum(p & 0x07E0 >= 0x600 for p in rows[64]) >= 36


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
