"""Export measurements from the pinned display library into target capabilities."""

import json
import re
from pathlib import Path

import tyro
from pydantic import BaseModel

from ....patch_switcher import models
from .build_bitmap_fonts import FAMILIES


class Options(BaseModel, frozen=True):
    source: Path = (
        Path(__file__).parent
        / 'firmware/.tools/data/packages/teensy/hardware/avr/1.62.0'
        / 'libraries/ST7735_t3/src'
    )
    output: Path = Path(__file__).parent / 'patch-switcher-target.json'


def read_bits(data: bytes, offset: int, count: int, signed: bool = False) -> int:
    value = 0
    for i in range(offset, offset + count):
        value = value * 2 + ((data[i // 8] >> (7 - i % 8)) & 1)
    return value - (1 << count) if signed and value & (1 << (count - 1)) else value


def native_face(source: str, name: str, identifier: int) -> models.FontFace:
    arrays = {
        n: bytes(int(v, 0) for v in re.findall(r'0x[0-9a-fA-F]+|\d+', b))
        for n, b in re.findall(
            r'const unsigned char (\w+)\[\](?: CNTRL_FONT_FLASH)? = \{(.*?)\};',
            source,
            re.S,
        )
    }
    sizes: dict[int, models.FontMetrics] = {}
    for size, raw in re.findall(
        rf'const ILI9341_t3_font_t {name}_(\d+)(?: CNTRL_FONT_FLASH)? = \{{(.*?)\}};',
        source,
        re.S,
    ):
        fields = [f.strip() for f in raw.split(',') if f.strip()]
        index, data = arrays[fields[0]], arrays[fields[2]]
        (
            version,
            reserved,
            first,
            last,
            second,
            second_last,
            bi,
            bw,
            bh,
            bx,
            by,
            bd,
            line,
            cap,
        ) = (int(f) for f in fields[3:])
        if (version, reserved, first, last, second, second_last) != (
            1,
            0,
            32,
            126,
            0,
            0,
        ):
            raise ValueError(f'unsupported font encoding: {name}_{size}')
        glyphs = bytearray()
        top, bottom = 0, line
        for c in range(95):
            offset = read_bits(index, c * bi, bi) * 8
            if read_bits(data, offset, 3):
                raise ValueError('unsupported glyph encoding')
            offset += 3
            width = read_bits(data, offset, bw)
            offset += bw
            height = read_bits(data, offset, bh)
            offset += bh
            left = read_bits(data, offset, bx, True)
            offset += bx
            y = read_bits(data, offset, by, True)
            offset += by
            advance = read_bits(data, offset, bd)
            top = min(top, cap - height - y)
            bottom = max(bottom, cap - y)
            glyphs.extend([advance, left & 255, width])
        sizes[int(size)] = models.FontMetrics(
            line_height=bottom - top, top=-top, glyphs=glyphs.hex()
        )
    return models.FontFace(name=name, id=identifier, sizes=sizes)


def main() -> None:
    options = tyro.cli(Options)
    classic = models.FontFace(
        name='classic',
        id=0,
        sizes={
            8 * s: models.FontMetrics(
                line_height=8 * s, glyphs=bytes([6 * s, 0, 6 * s] * 95).hex()
            )
            for s in range(1, 13)
        },
    )
    faces = [classic] + [
        native_face((options.source / f'st7735_t3_font_{n}.c').read_text(), n, i)
        for i, n in enumerate(['Arial', 'ComicSansMS'], 1)
    ]
    faces += [
        native_face(
            (
                Path(__file__).parent / 'firmware/patch_switcher' / f'font_{n}.cpp'
            ).read_text(),
            n,
            i,
        )
        for i, (n, _, _) in enumerate(FAMILIES, 3)
    ]
    data = json.loads(options.output.read_text())
    data.update(
        display_width=160, display_height=128, fonts=[f.model_dump() for f in faces]
    )
    options.output.write_text(json.dumps(data, indent=2) + '\n')


if __name__ == '__main__':
    main()
