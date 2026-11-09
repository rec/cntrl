"""Prepare dimension-driven RGB565 animation streams for bounded playback."""

import struct
from pathlib import Path

from PIL import Image, ImageSequence
from pydantic import BaseModel, Field


class Stream(BaseModel, frozen=True):
    mirror_x: bool = False
    mirror_y: bool = False


class Geometry(BaseModel, frozen=True):
    width: int = Field(ge=1, le=65535)
    height: int = Field(ge=1, le=65535)
    streams: list[Stream] = Field(
        default_factory=lambda: [Stream()], min_length=1, max_length=65535
    )

    @property
    def frame_bytes(self) -> int:
        return 4 + self.width * self.height * len(self.streams) * 2


class AnimationInfo(BaseModel, frozen=True):
    frames: int
    duration_ms: int
    size: int
    width: int
    height: int
    streams: int


def encode_frames(frames: bytes, durations: list[int], geometry: Geometry) -> bytes:
    """Encode normal images, applying explicit transforms to each output stream."""
    if not 1 <= len(durations) <= 65535:
        raise ValueError('animation needs 1..65535 frames')
    if len(frames) != len(durations) * geometry.width * geometry.height * 2:
        raise ValueError('RGB565 data does not match the frame count and geometry')
    if any(type(d) is not int or not 1 <= d <= 0x7FFFFFFF for d in durations):
        raise ValueError('frame durations must be integers in 1..2147483647 ms')
    size = HEADER.size + len(durations) * geometry.frame_bytes
    if size > 0xFFFFFFFF:
        raise ValueError('prepared animation exceeds the 32-bit asset size')
    result = bytearray(
        HEADER.pack(
            b'CNSA',
            2,
            geometry.width,
            geometry.height,
            len(geometry.streams),
            len(durations),
            1,
            size,
        )
    )
    row_bytes = geometry.width * 2
    for i, duration in enumerate(durations):
        result.extend(struct.pack('<I', duration))
        for y in range(geometry.height):
            for s in geometry.streams:
                source_y = geometry.height - y - 1 if s.mirror_y else y
                start = (i * geometry.height + source_y) * row_bytes
                row = frames[start : start + row_bytes]
                if s.mirror_x:
                    row = b''.join(row[x : x + 2] for x in range(row_bytes - 2, -1, -2))
                result.extend(row)
    return bytes(result)


def inspect_asset(data: bytes) -> AnimationInfo:
    if len(data) < HEADER.size:
        raise ValueError('truncated animation header')
    magic, version, width, height, streams, count, pixel_format, size = (
        HEADER.unpack_from(data)
    )
    if magic != b'CNSA' or version != 2 or pixel_format != 1:
        raise ValueError('expected a version-2 RGB565 CNSA animation asset')
    frame_bytes = 4 + width * height * streams * 2
    if (
        not width
        or not height
        or not streams
        or not count
        or size != len(data)
        or size != HEADER.size + count * frame_bytes
    ):
        raise ValueError('animation geometry, frame count and size disagree')
    durations = [
        struct.unpack_from('<I', data, HEADER.size + i * frame_bytes)[0]
        for i in range(count)
    ]
    if any(not 1 <= d <= 0x7FFFFFFF for d in durations):
        raise ValueError('invalid animation frame duration')
    return AnimationInfo(
        frames=count,
        duration_ms=sum(durations),
        size=size,
        width=width,
        height=height,
        streams=streams,
    )


def prepare_movie(source: Path, geometry: Geometry) -> bytes:
    """Convert image animations or stills to logical output streams."""
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
            scale = max(
                round(geometry.width * 1.2) / f.width,
                round(geometry.height * 1.2) / f.height,
            )
            enlarged = f.convert('RGB').resize(
                (round(f.width * scale), round(f.height * scale)),
                Image.Resampling.LANCZOS,
            )
            left = (enlarged.width - geometry.width) // 2
            top = (enlarged.height - geometry.height) // 2
            cropped = enlarged.crop(
                (left, top, left + geometry.width, top + geometry.height)
            )
            pixels = cropped.tobytes()
            for i in range(0, len(pixels), 3):
                r, g, b = pixels[i : i + 3]
                frames.extend(
                    struct.pack('>H', (r >> 3) << 11 | (g >> 2) << 5 | b >> 3)
                )
    return encode_frames(bytes(frames), durations, geometry)


HEADER = struct.Struct('<4s6HI')
