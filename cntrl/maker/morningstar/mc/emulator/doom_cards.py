"""Render the two MC6 Pro Doom control cards as RGB565 display images."""

from functools import cache
from pathlib import Path
from struct import pack

from PIL import Image, ImageDraw, ImageFont

from .....patch_chooser.graphics import rgb565
from .doom import HEIGHT, WIDTH


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


FONT = Path(__file__).resolve().parents[4] / 'fonts/metal-lord/MetalLord.otf'
