"""Generate missing cycle formatting without changing authored styles."""

from itertools import product
from random import Random

from . import graphics, models


def cycle_styles(
    show: models.Show, names: list[str], target: models.Target
) -> dict[str, models.Style]:
    if not show.fill_in_styles or not names:
        return {}
    background = graphics.color(show.background_color)
    random = Random(show.seed)
    # A bounded RGB lattice includes all cube corners. Greedy maximin selection
    # spreads colors away from both the background anchor and earlier choices.
    candidates = list(product([*range(0, 256, 32), 255], repeat=3))
    random.shuffle(candidates)
    distances = [
        sum((a - b) ** 2 for a, b in zip(c, background, strict=True))
        for c in candidates
    ]
    foregrounds: list[str] = []
    for _ in names:
        index = max(range(len(candidates)), key=lambda i: distances[i])
        chosen = candidates.pop(index)
        distances.pop(index)
        foregrounds.append('0x%02X%02X%02X' % chosen)
        distances = [
            min(d, sum((a - b) ** 2 for a, b in zip(c, chosen, strict=True)))
            for c, d in zip(candidates, distances, strict=True)
        ]
    fonts = list(dict.fromkeys([f.name for f in target.fonts] + list(show.fonts)))
    available: list[str] = []
    previous: str | None = None
    result: dict[str, models.Style] = {}
    for name, foreground in zip(names, foregrounds, strict=True):
        if not available:
            available = random.sample(fonts, len(fonts))
            if len(available) > 1 and available[-1] == previous:
                available[0], available[-1] = available[-1], available[0]
        previous = available.pop()
        result[name] = models.Style(
            font_name=previous,
            background_color=show.background_color,
            foreground_color=foreground,
        )
    return result
