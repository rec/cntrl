"""Render the two MC6 Pro Doom control cards as RGB565 display images."""

from functools import cache
from itertools import groupby
from pathlib import Path
from struct import iter_unpack, pack

import tyro
from PIL import Image, ImageDraw, ImageFont
from pydantic import BaseModel

from .....patch_chooser.graphics import rgb565

WIDTH, HEIGHT = 160, 128


class ControlCardExport(BaseModel, frozen=True):
    output: Path


@cache
def control_cards() -> tuple[bytes, bytes]:
    """Return left and right control cards in display wire order."""
    return (
        render_card(
            [
                ('Shoot', 18, 5, 5, 'lt'),
                ('Left', 18, 5, HEIGHT - 5, 'lb'),
            ]
        ),
        render_card(
            [
                ('Press to Activate', 14, WIDTH - 5, 5, 'rt'),
                ('Long Press to Flee', 14, WIDTH - 5, 23, 'rt'),
                ('Right', 18, WIDTH - 5, HEIGHT - 5, 'rb'),
            ]
        ),
    )


def render_card(labels: list[tuple[str, int, int, int, str]]) -> bytes:
    scale = 3
    image = Image.new('RGB', (WIDTH * scale, HEIGHT * scale))
    draw = ImageDraw.Draw(image)
    for label, size, x, y, anchor in labels:
        font = ImageFont.truetype(FONT, size * scale)
        draw.text(
            (x * scale, y * scale),
            label,
            font=font,
            anchor=anchor,
            fill='#ffcb53',
            stroke_width=scale,
            stroke_fill='#743500',
        )
    pixels = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS).tobytes()
    return pack(
        f'>{WIDTH * HEIGHT}H',
        *(rgb565(p) for p in zip(pixels[::3], pixels[1::3], pixels[2::3], strict=True)),
    )


def export_control_cards(output: Path) -> None:
    with output.open('w') as stream:
        for side, card in zip(('left', 'right'), control_cards(), strict=True):
            stream.write(
                '__attribute__((section(".progmem.doom_cards")))\n'
                f'constexpr CardRun {side}_card[] = {{\n'
            )
            for (color,), run in groupby(iter_unpack('>H', card)):
                stream.write(f'    {{{sum(1 for _ in run)}, 0x{color:04x}}},\n')
            stream.write('};\n')


FONT = Path(__file__).resolve().parents[4] / 'fonts/metal-lord/MetalLord.otf'


if __name__ == '__main__':
    export_control_cards(tyro.cli(ControlCardExport).output)
