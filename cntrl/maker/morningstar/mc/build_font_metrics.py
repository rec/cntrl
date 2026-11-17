"""Export measurements from the pinned display library into target capabilities."""

import json
import re
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel

from ....fonts.catalogue import FONT_IDS, UI_FONTS
from ....patch_switcher import models


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
    sizes: dict[int, models.FontMetrics] = {}
    for size, (index, data, numbers) in native_tables(source, name).items():
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
        ) = numbers
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


def native_tables(source: str, name: str) -> dict[int, tuple[bytes, bytes, list[int]]]:
    result: dict[int, tuple[bytes, bytes, list[int]]] = {}
    arrays = {
        n: bytes(int(v, 0) for v in re.findall(r'0x[0-9a-fA-F]+|\d+', b))
        for n, b in re.findall(
            r'const unsigned char (\w+)\[\](?: CNTRL_FONT_FLASH)? = \{(.*?)\};',
            source,
            re.S,
        )
    }
    for size, raw in re.findall(
        rf'const ILI9341_t3_font_t {name}_(\d+)(?: CNTRL_FONT_FLASH)? = \{{(.*?)\}};',
        source,
        re.S,
    ):
        fields = [f.strip() for f in raw.split(',') if f.strip()]
        index, data = arrays[fields[0]], arrays[fields[2]]
        result[int(size)] = index, data, [int(f) for f in fields[3:]]
    return result


def main() -> None:
    from .export_fonts import faces

    options = tyro.cli(Options)
    sources = {n: f'{n}.cfont' for n in FONT_IDS}
    exported = faces(Path(__file__).with_name('font_sources'), sources)
    text = options.output.read_text()
    data = (
        tomlkit.parse(text).unwrap()
        if options.output.suffix == '.toml'
        else json.loads(text)
    )
    data.update(
        fonts=[f.model_dump(mode='json') for f in exported],
        ui_fonts=list(UI_FONTS),
        font_sources={n: f'font_sources/{p}' for n, p in sources.items()},
    )
    for name in ('Arial', 'classic'):
        data.get('font_sources', {}).pop(name, None)
    options.output.write_text(
        tomlkit.dumps(data)
        if options.output.suffix == '.toml'
        else json.dumps(data, indent=2) + '\n'
    )


if __name__ == '__main__':
    main()
