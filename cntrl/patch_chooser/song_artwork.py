"""Song captions and controller-sized RGB565 images in library order."""

import struct
from hashlib import sha256
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageOps

from .. import source_files
from .graphics import rgb565
from .models import Target


def prepare(
    path: Path, target: Target, snapshots: dict[Path, bytes] | None
) -> tuple[int, int, bytes]:
    screen = target.displays[1 if len(target.displays) > 1 else 0]
    top = screen.height // 4
    bottom = screen.height * 3 // 4
    if len(target.displays) == 1:
        top = screen.height // 2
    size = screen.width, bottom - top
    with Image.open(BytesIO(source_files.read(path, snapshots))) as image:
        scaled = ImageOps.pad(
            image.convert('RGB'), size, method=Image.Resampling.LANCZOS
        )
    pixels = b''.join(
        struct.pack('<H', rgb565(p))
        for p in struct.iter_unpack('BBB', scaled.tobytes())
    )
    return *size, pixels


def unpack(data: bytes) -> list[tuple[str, str, bytes]]:
    if len(data) < 4:
        raise ValueError('truncated song artwork')
    count = struct.unpack_from('<I', data)[0]
    offset = 4 + count * ENTRY.size
    if offset > len(data):
        raise ValueError('truncated song artwork directory')
    result = []
    for index in range(count):
        label, width, height, position = ENTRY.unpack_from(data, 4 + index * ENTRY.size)
        end = position + width * height * 2
        if position != offset or end > len(data) or bool(width) != bool(height):
            raise ValueError('invalid song artwork dimensions or offset')
        name = ''
        png = b''
        if width:
            raw = data[position:end]
            stream = BytesIO()
            Image.frombytes('RGB', (width, height), raw, 'raw', 'BGR;16').save(
                stream, format='PNG'
            )
            png = stream.getvalue()
            name = f'images/{sha256(png).hexdigest()}.png'
        result.append((label.split(b'\0')[0].decode(), name, png))
        offset = end
    if offset != len(data):
        raise ValueError('unexpected trailing song artwork')
    return result


ENTRY = struct.Struct('<64sHHI')
