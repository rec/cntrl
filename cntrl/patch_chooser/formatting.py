"""Generate stable fallback styles for named objects."""

import re
from collections.abc import Iterable
from hashlib import blake2b
from itertools import product
from random import Random

from . import graphics, models


def styles(
    show: models.Setup,
    objects: Iterable[tuple[str, int | None]],
    target: models.Target,
    kind: str,
) -> dict[tuple[str, int | None], models.Style]:
    if not show.fill_in_styles or not (keys := dict.fromkeys(objects)):
        return {}
    background = graphics.color(show.background_color)
    channels = [*range(0, 256, 32), 255]
    candidates = [
        c
        for c in product(channels, channels, channels)
        if 299 * c[0] + 587 * c[1] + 114 * c[2]
        < show.foreground_brightness_limit * 255_000
        and graphics.distance(c, background) > show.contrast
    ]
    if not candidates:
        raise ValueError(
            'foreground brightness limit and contrast leave no eligible colors'
        )
    fonts = sorted(
        {
            f.name
            for f in target.fonts
            if f.name not in ('AtkinsonNextSemiBold', 'TamzenBold7x13')
        }
        | {re.sub(r'@[0-9.e+-]+x$', '', n) for n in show.fonts}
    )
    result: dict[tuple[str, int | None], models.Style] = {}
    for name, local_seed in keys:
        seed = show.seed if local_seed is None else local_seed
        identity = blake2b(f'{seed}:{kind}:{name}'.encode(), digest_size=16)
        random = Random(int.from_bytes(identity.digest(), 'big'))
        result[name, local_seed] = models.Style(
            font_name=random.choice(fonts),
            background_color=show.background_color,
            foreground_color='0x%02X%02X%02X' % random.choice(candidates),
        )
    return result
