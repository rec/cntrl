"""Show and instrument-table formats, separate from hardware catalogues."""

from functools import cached_property
from typing import Annotated

from pydantic import BaseModel, ConfigDict, Field, StrictStr, model_validator


class Bank(BaseModel, frozen=True):
    msb: Annotated[int, Field(strict=True, ge=0, le=127)] | None = None
    lsb: Annotated[int, Field(strict=True, ge=0, le=127)] | None = None

    @model_validator(mode='after')
    def require_component(self) -> 'Bank':
        if self.msb is None and self.lsb is None:
            raise ValueError('bank mapping needs msb or lsb')
        return self

    model_config = ConfigDict(extra='forbid')


class Setup(BaseModel, frozen=True):
    instrument: str
    name: str
    banks: dict[int, Bank] = Field(default_factory=dict)
    sysex: list[str] = Field(default_factory=list)

    model_config = ConfigDict(extra='forbid')


class Instrument(BaseModel, frozen=True):
    instrument: str
    setup: str
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]
    output: str

    model_config = ConfigDict(extra='forbid')


class Patch(BaseModel, frozen=True):
    instrument: str
    program: Annotated[int, Field(strict=True, ge=1, le=128)] | StrictStr
    volume: Annotated[int, Field(strict=True, ge=0, le=127)] | None = None
    pan: Annotated[int, Field(strict=True, ge=-64, le=63)] | None = None
    bank: Annotated[int, Field(strict=True, ge=0)] | None = None
    name: str | None = None
    display_name: str | None = None

    model_config = ConfigDict(extra='forbid')


class TablePatch(BaseModel, frozen=True):
    instrument: str
    setup: str
    program: Annotated[int, Field(strict=True, ge=1, le=128)]
    bank: Annotated[int, Field(strict=True, ge=0)] | None = None
    name: str | None = None
    display_name: str | None = None
    range: str | None = None
    description: str | None = None
    tags: list[str] = Field(default_factory=list)

    model_config = ConfigDict(extra='forbid')


class Definitions(BaseModel, frozen=True):
    setups: list[Setup]
    programs: list[TablePatch] = Field(default_factory=list)

    model_config = ConfigDict(extra='forbid')


class ScreenGraphics(BaseModel, frozen=True):
    background_color: str | None = None
    foreground_color: str | None = None
    half: str | None = None

    model_config = ConfigDict(extra='forbid')


class Screens(BaseModel, frozen=True):
    undo: ScreenGraphics | None = None
    sent: ScreenGraphics | None = None
    next: ScreenGraphics | None = None

    model_config = ConfigDict(extra='forbid')


class FontMetrics(BaseModel, frozen=True):
    line_height: int
    top: int = 0
    glyphs: str

    @cached_property
    def data(self) -> bytes:
        return bytes.fromhex(self.glyphs)


class FontFace(BaseModel, frozen=True):
    name: str
    id: int
    sizes: dict[int, FontMetrics]


class Show(BaseModel, frozen=True):
    # Validate entries independently so one malformed patch cannot hide others.
    instruments: dict[str, object]
    patches: dict[str, object]
    definitions: Definitions | None = None
    take_first_match: Annotated[bool, Field(strict=True)] = False
    chord_ms: Annotated[int, Field(strict=True, ge=1, le=65535)] = 100
    brightness: Annotated[int, Field(strict=True, ge=0, le=255)] = 192
    transition_ms: Annotated[int, Field(strict=True, ge=1, le=65535)] | None = None
    splash: Annotated[bool, Field(strict=True)] = True
    usb_midi_interface: Annotated[bool, Field(strict=True)] = False
    screensaver_timeout: Annotated[
        int,
        Field(
            strict=True,
            ge=0,
            le=2147483,
            description='Idle seconds; 0 disables the screensaver.',
        ),
    ] = 0
    font_name: str | None = None
    autoscale_fonts: Annotated[bool, Field(strict=True)] = False
    font_size: Annotated[int, Field(strict=True, gt=0, le=255)] | None = None
    pad: Annotated[int, Field(strict=True, ge=0, le=255)] | None = 4
    contrast: Annotated[float, Field(ge=0, le=1, allow_inf_nan=False)] = 0.6
    screens: Screens | None = None

    model_config = ConfigDict(extra='forbid')


class Target(BaseModel, frozen=True):
    display_width: int = Field(gt=0, le=65535)
    display_height: int = Field(gt=0, le=65535)
    fonts: list[FontFace]
    outputs: list[str]
    max_document_bytes: int = Field(gt=24, le=65536)
    max_instruments: int = Field(gt=0, le=32)
    max_categories: int = Field(gt=0, le=128)
    max_patches: int = Field(gt=0, le=65535)
    history_depth: int = Field(gt=0)
    characters: str

    model_config = ConfigDict(extra='forbid')
