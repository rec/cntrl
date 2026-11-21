"""Screen geometry referencing the existing controller catalogue."""

from pathlib import Path
from typing import Self

import tomlkit
from pydantic import BaseModel, ConfigDict, Field, ValidationError, model_validator
from tomlkit.exceptions import ParseError

from .. import catalogue, models
from .errors import EmulatorError, emulator_files


class Placement(BaseModel, frozen=True):
    x: float = Field(ge=0, allow_inf_nan=False)
    y: float = Field(ge=0, allow_inf_nan=False)
    width: float = Field(gt=0, allow_inf_nan=False)
    height: float = Field(gt=0, allow_inf_nan=False)

    def contains(self, x: float, y: float) -> bool:
        return self.x <= x < self.x + self.width and self.y <= y < self.y + self.height

    model_config = ConfigDict(extra='forbid')


class PortPlacement(Placement, frozen=True):
    port: int = Field(ge=0)


class EmulatorLayout(BaseModel, frozen=True):
    catalogue: Path
    controller: str
    width: float = Field(gt=0, allow_inf_nan=False)
    height: float = Field(gt=0, allow_inf_nan=False)
    faceplate: Path | None = None
    controls: dict[str, Placement] = Field(default_factory=dict)
    displays: dict[str, Placement] = Field(default_factory=dict)
    ports: list[PortPlacement] = Field(default_factory=list)
    provisional: bool = False

    @model_validator(mode='after')
    def bounds(self) -> Self:
        for p in [*self.controls.values(), *self.displays.values(), *self.ports]:
            if p.x + p.width > self.width or p.y + p.height > self.height:
                raise EmulatorError('component extends beyond the emulator canvas')
        if len({p.port for p in self.ports}) != len(self.ports):
            raise EmulatorError('duplicate port placement')
        return self

    @classmethod
    def load(cls, path: Path) -> 'EmulatorLayout':
        try:
            with emulator_files():
                text = path.read_text()
            layout = cls.model_validate(tomlkit.loads(text).unwrap())
        except (UnicodeDecodeError, ParseError, ValidationError) as error:
            raise EmulatorError(f'{path}: {error}') from error
        with emulator_files():
            return layout.model_copy(
                update={
                    'catalogue': (path.parent / layout.catalogue).resolve(),
                    'faceplate': (path.parent / layout.faceplate).resolve()
                    if layout.faceplate is not None
                    else None,
                }
            )

    def hardware(self) -> models.Controller:
        try:
            with emulator_files():
                controllers = catalogue.load_catalogue(self.catalogue)
        except (UnicodeDecodeError, ParseError, ValidationError) as error:
            raise EmulatorError(f'{self.catalogue}: {error}') from error
        if self.controller not in controllers:
            raise EmulatorError(f'unknown controller: {self.controller}')
        controller = controllers[self.controller]
        controls = [c.id for c in controller.controls]
        displays = [d.id for d in controller.displays]
        if len(set(controls)) != len(controls) or len(set(displays)) != len(displays):
            raise EmulatorError('controller component IDs must be unique')
        if set(self.controls) != set(controls) or set(self.displays) != set(displays):
            raise EmulatorError(
                'layout must place every controller control and display exactly once'
            )
        if any(p.port >= len(controller.ports) for p in self.ports):
            raise EmulatorError('layout references an unknown port')
        return controller

    model_config = ConfigDict(extra='forbid')


class Canvas:
    def __init__(self, layout: EmulatorLayout, width: float, height: float) -> None:
        self.layout = layout
        self.scale = min(width / layout.width, height / layout.height)
        self.left = (width - layout.width * self.scale) / 2
        self.top = (height - layout.height * self.scale) / 2

    def control(self, x: float, y: float) -> str | None:
        if self.scale <= 0:
            return None
        x, y = (x - self.left) / self.scale, (y - self.top) / self.scale
        return next(
            (n for n, p in self.layout.controls.items() if p.contains(x, y)), None
        )
