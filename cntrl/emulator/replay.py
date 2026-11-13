"""Deterministic TOML input scenarios using the interactive session protocol."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Literal, Self

import tomlkit
from pydantic import BaseModel, ConfigDict, Field, model_validator
from reccy.configuration import units

if TYPE_CHECKING:
    from .backend import Session


class Event(BaseModel, frozen=True):
    at: Annotated[
        float, Field(ge=0, allow_inf_nan=False), units.unit_validator('second')
    ]
    kind: Literal[
        'button',
        'midi',
        'block',
        'connection',
        'expression',
        'auxiliary',
        'text',
        'cleanup',
    ]
    target: str = ''
    value: int = 0
    data: list[Annotated[int, Field(ge=0, le=255)]] = Field(default_factory=list)
    text: str = ''

    @model_validator(mode='after')
    def valid_input(self) -> Self:
        if self.kind in (
            'button',
            'block',
            'connection',
            'text',
        ) and self.value not in (0, 1):
            raise ValueError(f'{self.kind} event value must be 0 or 1')
        if self.kind in ('button', 'midi', 'block', 'connection') and not self.target:
            raise ValueError(f'{self.kind} event requires a target')
        if self.kind == 'midi' and not self.data:
            raise ValueError('MIDI event requires message bytes')
        if self.kind == 'expression':
            if self.target not in ('0', '1', '2', '3') or not 0 <= self.value <= 1023:
                raise ValueError(
                    'expression requires a port 0..3 and raw value 0..1023'
                )
        if self.kind == 'auxiliary':
            if self.target not in ('0', '1', '2', '3') or not 0 <= self.value <= 3:
                raise ValueError('auxiliary requires a port 0..3 and contact mask 0..3')
        if self.kind == 'text' and not self.text.isascii():
            raise ValueError('text message must contain ASCII characters')
        return self

    model_config = ConfigDict(extra='forbid')


class Scenario(BaseModel, frozen=True):
    seed: int = Field(default=1, ge=0, le=0xFFFFFFFF)
    until: Annotated[
        float, Field(ge=0, allow_inf_nan=False), units.unit_validator('second')
    ]
    events: list[Event] = Field(default_factory=list)

    @model_validator(mode='after')
    def chronological(self) -> Self:
        times = [e.at for e in self.events]
        if times != sorted(times) or (times and times[-1] > self.until):
            raise ValueError('scenario events must be chronological and end by until')
        return self

    @classmethod
    def load(cls, path: Path) -> Scenario:
        return cls.model_validate(tomlkit.loads(path.read_text()).unwrap())

    def run(self, session: Session) -> None:
        for event in self.events:
            if event.kind == 'button' and event.target not in session.target.buttons:
                raise ValueError(f'unknown button: {event.target}')
            if event.kind in ('midi', 'block', 'connection') and not any(
                e.name == event.target
                and (
                    True
                    if event.kind == 'connection'
                    else e.input
                    if event.kind == 'midi'
                    else e.output
                )
                for e in session.target.midi_endpoints
            ):
                raise ValueError(
                    f'unknown MIDI endpoint for {event.kind}: {event.target}'
                )
        session.seed(self.seed)
        for event in self.events:
            session.advance(round(event.at * 1000000))
            if event.kind == 'button':
                session.button(event.target, bool(event.value))
            elif event.kind == 'midi':
                session.midi(event.target, bytes(event.data))
            elif event.kind == 'block':
                session.block(event.target, bool(event.value))
            elif event.kind == 'connection':
                session.connection(event.target, bool(event.value))
            elif event.kind == 'expression':
                session.expression(int(event.target), event.value)
            elif event.kind == 'auxiliary':
                session.auxiliary(int(event.target), event.value)
            elif event.kind == 'cleanup':
                session.cleanup()
            else:
                session.text(event.text, bool(event.value))
        session.advance(round(self.until * 1000000))

    model_config = ConfigDict(extra='forbid')
