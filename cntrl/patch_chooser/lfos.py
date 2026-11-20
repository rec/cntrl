"""Typed LFO configuration and its versioned generator parameters."""

import struct
from fractions import Fraction
from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator
from reccy.configuration import units


class Target(BaseModel, frozen=True):
    type: Literal[
        'control-change',
        'pitch-bend',
        'channel-pressure',
        'polyphonic-key-pressure',
        'nrpn',
        'rpn',
        'program-change',
    ]
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]
    controller: Annotated[int, Field(strict=True, ge=0, le=119)] | None = None
    note: Annotated[int, Field(strict=True, ge=0, le=127)] | None = None
    parameter: Annotated[int, Field(strict=True, ge=0, le=16383)] | None = None
    resolution: Literal[7, 14] | None = None

    @model_validator(mode='after')
    def address(self) -> 'Target':
        expected = {
            'control-change': {'controller', 'resolution'},
            'polyphonic-key-pressure': {'note'},
            'nrpn': {'parameter', 'resolution'},
            'rpn': {'parameter', 'resolution'},
        }.get(self.type, set())
        supplied = {
            n
            for n in ('controller', 'note', 'parameter', 'resolution')
            if getattr(self, n) is not None
        }
        if supplied - expected or expected - supplied - {'resolution'}:
            raise ValueError(f'{self.type}: requires only {sorted(expected)}')
        if self.type == 'control-change':
            assert self.controller is not None
            if self.controller in SPECIAL_CONTROLLERS:
                raise ValueError('LFO controller must address a continuous value')
            if self.resolution == 14 and self.controller > 31:
                raise ValueError('14-bit CC requires an MSB controller in 0..31')
        if self.type == 'rpn' and (
            self.parameter != 1 or self.resolution not in (None, 14)
        ):
            raise ValueError('continuous RPN requires parameter 1 and resolution 14')
        return self

    @property
    def bits(self) -> int:
        return self.resolution or (7 if self.type == 'control-change' else 14)

    @property
    def bounds(self) -> tuple[int, int]:
        if self.type == 'pitch-bend':
            return -8192, 8191
        if self.type == 'program-change':
            return 1, 128
        if self.type in ('control-change', 'nrpn', 'rpn') and self.bits == 14:
            return 0, 16383
        return 0, 127

    @property
    def kind(self) -> int:
        return (
            (2 if self.bits == 14 else 1)
            if self.type == 'control-change'
            else (7 if self.bits == 14 else 6)
            if self.type == 'nrpn'
            else KINDS[self.type]
        )

    @property
    def address_number(self) -> int:
        return self.controller or self.note or self.parameter or 0

    model_config = ConfigDict(extra='forbid')


class BeatPeriod(BaseModel, frozen=True):
    beats: str

    @field_validator('beats')
    @classmethod
    def positive_fraction(cls, value: str) -> str:
        try:
            fraction = Fraction(value)
        except ZeroDivisionError as error:
            raise ValueError('beat denominator must be nonzero') from error
        if not 0 < fraction <= 4096 or fraction.denominator > 65535:
            raise ValueError(
                'beats must be positive, at most 4096, denominator <= 65535'
            )
        return str(fraction)

    model_config = ConfigDict(extra='forbid')


class Off(BaseModel, frozen=True):
    action: Literal['hold', 'value', 'restore']
    value: Annotated[int, Field(strict=True)] | None = None

    @model_validator(mode='after')
    def final_value(self) -> 'Off':
        if (self.action == 'value') != (self.value is not None):
            raise ValueError('only off action value requires a value')
        return self

    model_config = ConfigDict(extra='forbid')


class Lfo(BaseModel, frozen=True):
    target: Target
    shape: Literal[
        'sine',
        'triangle',
        'saw-up',
        'saw-down',
        'square',
        'random',
        'trapezoid',
        'smooth-random',
    ]
    range: list[Annotated[int, Field(strict=True)]] = Field(min_length=2, max_length=2)
    period: (
        Annotated[
            float,
            Field(ge=0.001, le=3600, allow_inf_nan=False),
            units.unit_validator('second'),
        ]
        | BeatPeriod
    )
    off: Off
    phase: Annotated[float, Field(ge=0, lt=1, allow_inf_nan=False)] = 0
    phase_mode: Literal['retrigger', 'free'] = 'retrigger'
    run: Literal['repeat', 'once'] = 'repeat'
    update_interval: Annotated[
        float,
        Field(ge=0.001, le=60, allow_inf_nan=False),
        units.unit_validator('second'),
    ] = 0.01
    seed: Annotated[int, Field(strict=True, ge=0, le=4294967295)] = 0

    @model_validator(mode='after')
    def values(self) -> 'Lfo':
        low, high = self.target.bounds
        if any(not low <= v <= high for v in self.range):
            raise ValueError(f'LFO range must be within {low}..{high}')
        if self.off.value is not None and not low <= self.off.value <= high:
            raise ValueError(f'LFO off value must be within {low}..{high}')
        if self.run == 'once' and self.phase_mode == 'free':
            raise ValueError('one-shot LFOs require retrigger phase mode')
        if self.shape not in ('random', 'smooth-random') and self.seed:
            raise ValueError('seed is only meaningful for random LFO shapes')
        return self

    model_config = ConfigDict(extra='forbid')


def pack(value: Lfo) -> bytes:
    period = (
        Fraction(value.period.beats)
        if isinstance(value.period, BeatPeriod)
        else Fraction(round(value.period * 1_000_000))
    )
    return PARAMETERS.pack(
        value.target.kind,
        value.target.channel - 1,
        value.target.address_number,
        SHAPES.index(value.shape),
        value.run == 'once',
        value.phase_mode == 'free',
        isinstance(value.period, BeatPeriod),
        *value.range,
        period.numerator,
        period.denominator,
        min(4294967295, round(value.phase * (1 << 32))),
        round(value.update_interval * 1_000_000),
        value.seed,
        ('hold', 'value', 'restore').index(value.off.action),
        value.off.value or 0,
    )


SPECIAL_CONTROLLERS = {
    0,
    6,
    32,
    38,
    64,
    65,
    66,
    67,
    68,
    69,
    84,
    88,
    96,
    97,
    98,
    99,
    100,
    101,
}
KINDS = {
    'pitch-bend': 3,
    'channel-pressure': 4,
    'polyphonic-key-pressure': 5,
    'rpn': 8,
    'program-change': 9,
}
SHAPES = (
    'sine',
    'triangle',
    'saw-up',
    'saw-down',
    'square',
    'random',
    'trapezoid',
    'smooth-random',
)
PARAMETERS = struct.Struct('<BBHBBBBiiQIIIIBi')
