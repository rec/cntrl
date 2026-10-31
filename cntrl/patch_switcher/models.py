"""Show and instrument-table formats, separate from hardware catalogues."""

import re
from functools import cached_property
from typing import Annotated, Literal

from pydantic import (
    BaseModel,
    BeforeValidator,
    ConfigDict,
    Field,
    StrictStr,
    TypeAdapter,
    field_validator,
    model_validator,
)
from reccy.configuration import units


def parse_tags(value: object) -> object:
    if isinstance(value, str):
        return [t.strip() for t in re.split('[,:]', value) if t.strip()]
    return value


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
    setup: str = 'default'
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]
    output: str

    model_config = ConfigDict(extra='forbid')


class Style(BaseModel, frozen=True):
    icon: str | None = None
    background_color: str | None = None
    foreground_color: str | None = None
    font_name: str | None = None

    model_config = ConfigDict(extra='forbid')


class Tag(BaseModel, frozen=True):
    style: Style | None = None

    model_config = ConfigDict(extra='forbid')


class Presentation(BaseModel, frozen=True):
    name: str | None = None
    display_name: str | None = None
    style: Style | None = None
    tags: Annotated[list[str], BeforeValidator(parse_tags)] = Field(
        default_factory=list
    )

    model_config = ConfigDict(extra='forbid')


class ProgramChange(Presentation, frozen=True):
    type: Literal['program-change']
    instrument: str
    program: Annotated[int, Field(strict=True, ge=1, le=128)] | StrictStr
    volume: Annotated[int, Field(strict=True, ge=0, le=127)] | None = None
    pan: Annotated[int, Field(strict=True, ge=-64, le=63)] | None = None
    bank: Annotated[int, Field(strict=True, ge=0)] | None = None


class ControlChange(Presentation, frozen=True):
    type: Literal['control-change']
    output: str
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]
    controller: Annotated[int, Field(strict=True, ge=0, le=127)]
    value: Annotated[int, Field(strict=True, ge=0, le=127)]


class PitchBend(Presentation, frozen=True):
    type: Literal['pitch-bend']
    output: str
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]
    value: Annotated[int, Field(strict=True, ge=-8192, le=8191)]


class Nrpn(Presentation, frozen=True):
    type: Literal['nrpn']
    output: str
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]
    parameter: Annotated[int, Field(strict=True, ge=0, le=16383)]
    value: Annotated[int, Field(strict=True, ge=0, le=16383)]


class SysEx(Presentation, frozen=True):
    type: Literal['sysex']
    output: str
    data: list[Annotated[int, Field(strict=True, ge=0, le=127)]]


class Midi(Presentation, frozen=True):
    type: Literal['midi']
    output: str
    bytes: list[Annotated[int, Field(strict=True, ge=0, le=255)]]


class Brightness(Presentation, frozen=True):
    type: Literal['state']
    operation: Literal['brightness']
    value: Annotated[int, Field(strict=True, ge=0, le=255)]


class UsbMidiInterface(Presentation, frozen=True):
    type: Literal['state']
    operation: Literal['usb-midi-interface']
    value: Annotated[bool, Field(strict=True)]


class Navigation(Presentation, frozen=True):
    type: Literal['state']
    operation: Literal[
        'next-patch',
        'previous-patch',
        'next-patch-cycle',
        'previous-patch-cycle',
        'activate-selection',
        'undo',
        'menu',
        'back',
    ]


class StockFirmware(Presentation, frozen=True):
    type: Literal['stock-firmware']
    exit_buttons: str

    @field_validator('exit_buttons')
    @classmethod
    def button_pair(cls, value: str) -> str:
        if (
            len(value) != 3
            or value[1] != '+'
            or any(c not in 'ABCDEF' for c in (value[0], value[2]))
            or value[0] == value[2]
        ):
            raise ValueError(
                'exit_buttons must be two distinct buttons, for example A+F'
            )
        return '+'.join(sorted((value[0], value[2])))

    @property
    def button_mask(self) -> int:
        return sum(1 << 'ABCDEF'.index(c) for c in self.exit_buttons.split('+'))


class Sequence(Presentation, frozen=True):
    type: Literal['sequence']
    patches: list['Patch']


class Reference(Presentation, frozen=True):
    type: Literal['reference']
    patch: str
    document: str | None = None


Patch = Annotated[
    ProgramChange
    | ControlChange
    | PitchBend
    | Nrpn
    | SysEx
    | Midi
    | Annotated[
        Brightness | UsbMidiInterface | Navigation, Field(discriminator='operation')
    ]
    | StockFirmware
    | Sequence
    | Reference,
    Field(discriminator='type'),
]
Sequence.model_rebuild()
patch_adapter = TypeAdapter(Patch)


class TablePatch(BaseModel, frozen=True):
    instrument: str
    setup: str = 'default'
    program: Annotated[int, Field(strict=True, ge=1, le=128)]
    bank: Annotated[int, Field(strict=True, ge=0)] | None = None
    name: str | None = None
    display_name: str | None = None
    names: dict[str, str] = Field(default_factory=dict)
    range: str | None = None
    description: str | None = None
    style: Style | None = None
    tags: Annotated[list[str], BeforeValidator(parse_tags)] = Field(
        default_factory=list
    )

    model_config = ConfigDict(extra='forbid')


class Definitions(BaseModel, frozen=True):
    setups: list[Setup]
    programs: list[TablePatch] = Field(default_factory=list)

    model_config = ConfigDict(extra='forbid')


class Page(BaseModel, frozen=True):
    fields: list[str] = Field(
        default_factory=lambda: ['patch_cycle.name', 'display_name']
    )
    style: Style | None = None
    line_styles: dict[str, Style] = Field(default_factory=dict)

    @model_validator(mode='after')
    def unique_fields(self) -> 'Page':
        if len(self.fields) != len(set(self.fields)):
            raise ValueError('duplicate page field')
        if any(n not in self.fields for n in self.line_styles):
            raise ValueError('line_styles must refer to fields on this page')
        return self

    model_config = ConfigDict(extra='forbid')


class Window(BaseModel, frozen=True):
    page: str = 'patch'
    pad: (
        Annotated[int, Field(strict=True, ge=0, le=255), units.unit_validator('pixel')]
        | None
    ) = None
    style: Style | None = None

    model_config = ConfigDict(extra='forbid')


class Windows(BaseModel, frozen=True):
    undo: Window = Field(default_factory=Window)
    sent: Window = Field(default_factory=Window)
    next: Window = Field(default_factory=Window)

    model_config = ConfigDict(extra='forbid')


class Parameter(BaseModel, frozen=True):
    value: str | int | float | bool | None = None
    style: Style | None = None

    model_config = ConfigDict(extra='forbid', strict=True, allow_inf_nan=False)


class FontMetrics(BaseModel, frozen=True):
    line_height: units.Pixels
    top: units.Pixels = 0
    glyphs: str = ''
    runs: dict[str, list[int]] = Field(default_factory=dict)
    characters: dict[str, list[int]] = Field(default_factory=dict)

    @cached_property
    def data(self) -> bytes:
        return bytes.fromhex(self.glyphs)


class FontFace(BaseModel, frozen=True):
    name: str
    id: int
    sizes: dict[units.Pixels, FontMetrics]
    asset: int = 0


class SwitchAssignment(BaseModel, frozen=True):
    name: str | None = None
    button: Literal['A', 'B', 'C', 'D', 'E']
    mode: Literal['press', 'momentary', 'latched'] = 'press'
    on: Patch
    off: Patch | None = None
    style: Style | None = None

    @model_validator(mode='after')
    def cleanup(self) -> 'SwitchAssignment':
        if self.mode == 'press' and self.off is not None:
            raise ValueError('press switches forbid off')
        if self.mode != 'press' and self.off is None:
            raise ValueError('momentary and latched switches require off')
        return self

    model_config = ConfigDict(extra='forbid')


class Show(BaseModel, frozen=True):
    # Validate entries independently so one malformed patch cannot hide others.
    instruments: dict[str, object] = Field(default_factory=dict)
    patches: dict[str, object] = Field(default_factory=dict)
    patch_cycles: dict[str, object] = Field(default_factory=dict)
    definitions: Definitions | None = None
    fonts: dict[str, str] = Field(default_factory=dict)
    take_first_match: Annotated[bool, Field(strict=True)] = False
    chord_ms: Annotated[
        int, Field(strict=True, ge=1, le=65535), units.unit_validator('millisecond')
    ] = 100
    brightness: Annotated[int, Field(strict=True, ge=0, le=255)] = 192
    transition_ms: (
        Annotated[
            int, Field(strict=True, ge=1, le=65535), units.unit_validator('millisecond')
        ]
        | None
    ) = None
    splash: Annotated[bool, Field(strict=True)] = True
    animation_color_cycle: Annotated[bool, Field(strict=True)] = False
    usb_midi_interface: Annotated[bool, Field(strict=True)] = False
    screensaver_timeout: Annotated[
        int,
        Field(
            strict=True,
            ge=0,
            le=2147483,
            description='Idle seconds; 0 disables the screensaver.',
        ),
        units.unit_validator('second', exact=True),
    ] = 0
    style: Style | None = None
    pad: (
        Annotated[int, Field(strict=True, ge=0, le=255), units.unit_validator('pixel')]
        | None
    ) = 4
    contrast: Annotated[
        float,
        Field(ge=0, le=1, allow_inf_nan=False),
        units.unit_validator('dimensionless'),
    ] = 0.6
    windows: Windows = Field(default_factory=Windows)
    pages: dict[str, Page] = Field(default_factory=lambda: {'patch': Page()})
    tags: dict[str, Tag] = Field(default_factory=dict)
    parameters: dict[str, Parameter] = Field(default_factory=dict)
    switches: dict[str, object] = Field(default_factory=dict)

    model_config = ConfigDict(extra='forbid')


class Target(BaseModel, frozen=True):
    functions: list[str] = Field(default_factory=list)
    buttons: list[str] = Field(default_factory=list)
    max_switches: Annotated[int, Field(strict=True, ge=0, le=5)] = 5
    display_width: units.Pixels = Field(gt=0, le=65535)
    display_height: units.Pixels = Field(gt=0, le=65535)
    fonts: list[FontFace]
    outputs: list[str]
    max_document_bytes: units.Bytes = Field(gt=24, le=65536)
    max_instruments: int = Field(gt=0, le=32)
    max_patch_cycles: int = Field(gt=0, le=128)
    max_patches: int = Field(gt=0, le=65535)
    history_depth: int = Field(gt=0)
    characters: str

    model_config = ConfigDict(extra='forbid')
