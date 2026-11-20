"""Typed, sparse device preferences shared by transport and recovery."""

from typing import Annotated

from pydantic import BaseModel, ConfigDict, Field, field_validator

from . import device_settings


class Overlay(BaseModel, frozen=True):
    startup_set: (
        Annotated[str, Field(min_length=1, max_length=63, pattern=r'^[ -~]+$')] | None
    ) = None
    values: dict[int, Annotated[int, Field(strict=True, ge=0, le=0xFFFFFFFF)]] = Field(
        default_factory=dict
    )
    language: (
        Annotated[
            str,
            Field(
                min_length=2,
                max_length=15,
                pattern=r'^[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$',
            ),
        ]
        | None
    ) = None

    @field_validator('values')
    @classmethod
    def valid_settings(cls, value: dict[int, int]) -> dict[int, int]:
        for field, raw in value.items():
            device_settings.decode_value(field, raw)
        return value

    model_config = ConfigDict(extra='forbid')
