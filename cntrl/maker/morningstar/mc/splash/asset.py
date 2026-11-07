"""Mac-side preparation of full-bleed, pre-mirrored RGB565 splash frames."""

import struct
from pathlib import Path

from PIL import Image, ImageOps, ImageSequence
from pydantic import BaseModel


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


def prepare_movie(source: Path) -> bytes:
    """Convert GIF/WebP animations or PNG/JPEG stills with Pillow."""
    frames = bytearray()
    durations: list[int] = []
    with Image.open(source) as image:
        if image.format not in ('GIF', 'WEBP', 'PNG', 'JPEG'):
            raise ValueError(f'{source}: expected GIF, WebP, PNG or JPEG')
        default_duration = 100 if getattr(image, 'is_animated', False) else 1000
        for f in ImageSequence.Iterator(image):
            f.load()
            durations.append(
                int(f.info.get('duration', default_duration) or default_duration)
            )
            # Enlarge by 120%, preserving proportions, then take the centre.
            scale = max(192 / f.width, 154 / f.height)
            enlarged = f.convert('RGB').resize(
                (round(f.width * scale), round(f.height * scale)),
                Image.Resampling.LANCZOS,
            )
            cropped = ImageOps.crop(
                enlarged,
                (
                    (enlarged.width - WIDTH) // 2,
                    (enlarged.height - HEIGHT) // 2,
                    (enlarged.width - WIDTH + 1) // 2,
                    (enlarged.height - HEIGHT + 1) // 2,
                ),
            )
            pixels = cropped.tobytes()
            for i in range(0, len(pixels), 3):
                r, g, b = pixels[i : i + 3]
                frames.extend(
                    struct.pack('>H', (r >> 3) << 11 | (g >> 2) << 5 | b >> 3)
                )
    return encode_frames(bytes(frames), durations)


WIDTH = 160
HEIGHT = 128
HEADER = struct.Struct('<4sHHHHI')
FRAME_BYTES = 4 + WIDTH * HEIGHT * 4
