"""Show and instrument-table formats, separate from hardware catalogues."""

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


class Definitions(BaseModel, frozen=True):
    setups: list[Setup]

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

    model_config = ConfigDict(extra='forbid')


class TablePatch(BaseModel, frozen=True):
    instrument: str
    setup: str
    program: Annotated[int, Field(strict=True, ge=1, le=128)]
    bank: Annotated[int, Field(strict=True, ge=0)] | None = None
    name: str | None = None
    range: str | None = None
    description: str | None = None
    tags: list[str] = Field(default_factory=list)

    model_config = ConfigDict(extra='forbid')


class Show(BaseModel, frozen=True):
    # Validate entries independently so one malformed patch cannot hide others.
    instruments: dict[str, object]
    patches: dict[str, object]
    take_first_match: Annotated[bool, Field(strict=True)] = False
    chord_ms: Annotated[int, Field(strict=True, ge=1, le=65535)] = 100

    model_config = ConfigDict(extra='forbid')


class Target(BaseModel, frozen=True):
    outputs: list[str]
    max_document_bytes: int = Field(gt=24, le=65536)
    max_instruments: int = Field(gt=0, le=32)
    max_categories: int = Field(gt=0, le=128)
    max_patches: int = Field(gt=0, le=65535)
    history_depth: int = Field(gt=0)
    characters: str

    model_config = ConfigDict(extra='forbid')
