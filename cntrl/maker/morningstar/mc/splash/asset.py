"""Mac-side preparation of full-bleed, pre-mirrored RGB565 splash frames."""

import struct
import subprocess
from decimal import ROUND_HALF_UP, Decimal
from pathlib import Path

from pydantic import BaseModel, Field


class Timing(BaseModel, frozen=True):
    duration_time: Decimal = Field(gt=0)


class Probe(BaseModel, frozen=True):
    frames: list[Timing]


class AnimationInfo(BaseModel, frozen=True):
    frames: int
    duration_ms: int
    size: int


def encode_frames(frames: bytes, durations: list[int]) -> bytes:
    """Prepare normal outer-screen and mirrored centre-screen rows on the Mac."""
    if not 1 <= len(durations) <= 65535:
        raise ValueError('animation needs 1..65535 frames')
    if len(frames) != len(durations) * WIDTH * HEIGHT * 2:
        raise ValueError('RGB565 data does not match the frame count')
    if any(type(d) is not int or not 1 <= d <= 0x7FFFFFFF for d in durations):
        raise ValueError('frame durations must be integers in 1..2147483647 ms')
    size = HEADER.size + len(durations) * FRAME_BYTES
    if size > 0xFFFFFFFF:
        raise ValueError('prepared animation exceeds the 32-bit asset size')
    result = bytearray(HEADER.pack(b'CNSA', 1, WIDTH, HEIGHT, len(durations), size))
    for i, duration in enumerate(durations):
        result.extend(struct.pack('<I', duration))
        for y in range(HEIGHT):
            start = (i * HEIGHT + y) * WIDTH * 2
            row = frames[start : start + WIDTH * 2]
            result.extend(row)
            result.extend(b''.join(row[x : x + 2] for x in range(len(row) - 2, -1, -2)))
    return bytes(result)


def inspect_asset(data: bytes) -> AnimationInfo:
    if len(data) < HEADER.size:
        raise ValueError('truncated splash header')
    magic, version, width, height, count, size = HEADER.unpack_from(data)
    if (magic, version, width, height) != (b'CNSA', 1, WIDTH, HEIGHT):
        raise ValueError('expected a version-1, 160x128 CNSA splash asset')
    if not count or size != len(data) or size != HEADER.size + count * FRAME_BYTES:
        raise ValueError('splash frame count and size disagree')
    durations = [
        struct.unpack_from('<I', data, HEADER.size + i * FRAME_BYTES)[0]
        for i in range(count)
    ]
    if any(not 1 <= d <= 0x7FFFFFFF for d in durations):
        raise ValueError('invalid splash frame duration')
    return AnimationInfo(frames=count, duration_ms=sum(durations), size=size)


def prepare_gif(source: Path) -> bytes:
    """FFmpeg handles GIF disposal/compositing, crop, scaling and RGB conversion."""
    probe = subprocess.run(
        [
            'ffprobe',
            '-v',
            'error',
            '-select_streams',
            'v:0',
            '-show_frames',
            '-show_entries',
            'frame=duration_time',
            '-of',
            'json',
            str(source),
        ],
        capture_output=True,
        check=True,
    )
    timing = Probe.model_validate_json(probe.stdout)
    durations = [
        int((f.duration_time * 1000).to_integral_value(rounding=ROUND_HALF_UP))
        for f in timing.frames
    ]
    decoded = subprocess.run(
        [
            'ffmpeg',
            '-v',
            'error',
            '-i',
            str(source),
            '-map',
            '0:v:0',
            '-vf',
            'scale=192:154:force_original_aspect_ratio=increase:flags=lanczos,'
            'crop=160:128',
            '-fps_mode',
            'passthrough',
            '-f',
            'rawvideo',
            '-pix_fmt',
            'rgb565be',
            'pipe:1',
        ],
        capture_output=True,
        check=True,
    )
    return encode_frames(decoded.stdout, durations)


WIDTH = 160
HEIGHT = 128
HEADER = struct.Struct('<4sHHHHI')
FRAME_BYTES = 4 + WIDTH * HEIGHT * 4
