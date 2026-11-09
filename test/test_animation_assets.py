"""Prepared frame geometry and a real Python/C++ transfer over a pseudo-terminal."""

import struct
from pathlib import Path
from zlib import crc32

import pytest
from native_peer import NativePeer
from PIL import Image, ImageDraw

from cntrl.animation import asset, cli
from scripts import native_tests


def test_output_streams_apply_explicit_transforms() -> None:
    row = b''.join(struct.pack('>H', x) for x in range(GEOMETRY.width))
    data = asset.encode_frames(row * GEOMETRY.height * 2, [100, 200], GEOMETRY)
    info = asset.inspect_asset(data)
    assert info.frames == 2 and info.duration_ms == 300
    assert info.size == asset.HEADER.size + 2 * GEOMETRY.frame_bytes
    for frame, duration in enumerate([100, 200]):
        offset = asset.HEADER.size + frame * GEOMETRY.frame_bytes
        assert struct.unpack_from('<I', data, offset)[0] == duration
        for y in range(GEOMETRY.height):
            start = offset + 4 + y * GEOMETRY.width * 4
            assert data[start : start + GEOMETRY.width * 2] == row
            mirrored = data[start + GEOMETRY.width * 2 : start + GEOMETRY.width * 4]
            assert list(struct.unpack('>160H', mirrored)) == list(reversed(range(160)))


def test_animation_with_zero_frame_delays_uses_ten_frames_per_second(
    tmp_path: Path,
) -> None:
    source = tmp_path / 'zero.gif'
    image = Image.new('RGB', (20, 20), 'red')
    image.save(
        source,
        save_all=True,
        append_images=[Image.new('RGB', image.size, 'blue')],
        duration=0,
    )
    info = asset.inspect_asset(asset.prepare_movie(source, GEOMETRY))
    assert info.frames == 2
    assert info.duration_ms == 200


def test_gif_is_enlarged_and_centre_cropped(tmp_path: Path) -> None:
    source = tmp_path / 'border.gif'
    image = Image.new('RGB', (160, 128), 'blue')
    drawing = ImageDraw.Draw(image)
    drawing.rectangle((0, 0, 159, 127), outline='red', width=4)
    drawing.rectangle((64, 52, 95, 75), fill='lime')
    image.save(source)
    data = asset.prepare_movie(source, GEOMETRY)
    assert asset.inspect_asset(data).frames == 1
    rows = [
        struct.unpack_from('>160H', data, asset.HEADER.size + 4 + y * 640)
        for y in range(GEOMETRY.height)
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
            bytes(len(durations) * GEOMETRY.width * GEOMETRY.height * 2),
            durations,
            GEOMETRY,
        )


def test_malformed_asset_is_rejected_before_opening_serial() -> None:
    with pytest.raises(ValueError, match='header'):
        cli.upload_asset(Path('/not-a-device'), b'bad')
    data = asset.encode_frames(
        bytes(GEOMETRY.width * GEOMETRY.height * 2), [100], GEOMETRY
    )
    with pytest.raises(ValueError, match='size'):
        asset.inspect_asset(data[:-1])
    zero = bytearray(data)
    struct.pack_into('<I', zero, asset.HEADER.size, 0)
    with pytest.raises(ValueError, match='duration'):
        asset.inspect_asset(bytes(zero))


@pytest.fixture(scope='module')
def peer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    executable = tmp_path_factory.mktemp('splash-peer') / 'peer'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            f'-I{ROOT / "firmware/include"}',
            str(ROOT / 'firmware/test/animation/peer.cpp'),
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
        bytes(GEOMETRY.width * GEOMETRY.height * 2 * frames), [100] * frames, GEOMETRY
    )
    with NativePeer([str(peer)]) as port:
        result = cli.upload_asset(Path(port), data)
        assert result == f'CNTRL SPLASH READY {len(data)} {crc32(data):08x}'


ROOT = Path(__file__).resolve().parents[1]
GEOMETRY = asset.Geometry(
    width=160, height=128, streams=[asset.Stream(), asset.Stream(mirror_x=True)]
)


def test_vertical_stream_transform_uses_requested_geometry() -> None:
    geometry = asset.Geometry(width=2, height=3, streams=[asset.Stream(mirror_y=True)])
    data = asset.encode_frames(bytes(range(12)), [100], geometry)
    assert data[asset.HEADER.size + 4 :] == bytes(
        [8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3]
    )
