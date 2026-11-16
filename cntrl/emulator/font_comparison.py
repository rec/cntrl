"""Native-size Status font samples, independent of controller profiles."""

from pathlib import Path

import tomlkit
from pydantic import BaseModel, Field


class FontSample(BaseModel, frozen=True):
    name: str = Field(min_length=1, max_length=47)
    file: str
    size: int = Field(ge=8, le=96)


class FontComparison(BaseModel, frozen=True):
    samples: list[FontSample] = Field(min_length=1)


def load() -> list[FontSample]:
    return FontComparison.model_validate(
        tomlkit.parse((DIRECTORY / 'comparison.toml').read_text()).unwrap()
    ).samples


DIRECTORY = Path(__file__).parents[1] / 'fonts' / 'bitmap_samples'
