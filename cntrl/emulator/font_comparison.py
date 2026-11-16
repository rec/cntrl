"""Native-size Status font samples, independent of controller profiles."""

from functools import cached_property
from pathlib import Path

import tomlkit
from pydantic import BaseModel, Field

from ..fonts.bitmap import BitmapRaster


class FontSample(BaseModel, frozen=True):
    name: str = Field(min_length=1, max_length=47)
    file: str
    size: int = Field(ge=8, le=96)

    @cached_property
    def capital_height(self) -> int:
        glyph = BitmapRaster(DIRECTORY / self.file).glyph(self.size, ord('H'))
        stride = (glyph.width + 7) // 8
        rows = [
            y
            for y in range(glyph.height)
            if any(glyph.pixels[y * stride : (y + 1) * stride])
        ]
        return rows[-1] - rows[0] + 1 if rows else 0


class FontComparison(BaseModel, frozen=True):
    samples: list[FontSample] = Field(min_length=1)


def load() -> list[FontSample]:
    samples = FontComparison.model_validate(
        tomlkit.parse((DIRECTORY / 'comparison.toml').read_text()).unwrap()
    ).samples
    return [
        s.model_copy(update={'name': f'{s.name} (7px)'})
        for s in samples
        if s.capital_height == 7
    ]


DIRECTORY = Path(__file__).parents[1] / 'fonts' / 'bitmap_samples'
