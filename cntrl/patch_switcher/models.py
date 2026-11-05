"""Setup and instrument-table formats, separate from hardware catalogues."""

import re
from functools import cached_property
from typing import Annotated, Literal

from pydantic import (
    AfterValidator,
    BaseModel,
    BeforeValidator,
    ConfigDict,
    Discriminator,
    Field,
    StrictStr,
    Tag,
    TypeAdapter,
    field_validator,
    model_validator,
)
from reccy.configuration import units


def millisecond_resolution(value: float) -> float:
    if value != round(value * 1000) / 1000:
        raise ValueError('duration must be a whole number of milliseconds')
    return value


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


class InstrumentProfile(BaseModel, frozen=True):
    instrument: str
    name: str
    banks: dict[int, Bank] = Field(default_factory=dict)
    sysex: list[str] = Field(default_factory=list)

    model_config = ConfigDict(extra='forbid')


class Instrument(BaseModel, frozen=True):
    instrument: str
    profile: str = 'default'
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]
    output: str

    model_config = ConfigDict(extra='forbid')


class Style(BaseModel, frozen=True):
    icon: str | None = None
    background_color: str | None = None
    foreground_color: str | None = None
    font_name: str | None = None
    horizontal_scale: (
        Annotated[float, Field(strict=True, gt=0, allow_inf_nan=False)] | None
    ) = None

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


class OutputMessage(Presentation, frozen=True):
    type: str
    output: str


class ChannelMessage(OutputMessage, frozen=True):
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]


class ControlChange(ChannelMessage, frozen=True):
    type: Literal['control-change']
    controller: Annotated[int, Field(strict=True, ge=0, le=127)]
    value: Annotated[int, Field(strict=True, ge=0, le=127)]


class PitchBend(ChannelMessage, frozen=True):
    type: Literal['pitch-bend']
    value: Annotated[int, Field(strict=True, ge=-8192, le=8191)]


class Nrpn(ChannelMessage, frozen=True):
    type: Literal['nrpn']
    parameter: Annotated[int, Field(strict=True, ge=0, le=16383)]
    value: Annotated[int, Field(strict=True, ge=0, le=16383)]


class SysEx(OutputMessage, frozen=True):
    type: Literal['sysex']
    data: list[Annotated[int, Field(strict=True, ge=0, le=127)]]


class ChannelProgramChange(ChannelMessage, frozen=True):
    type: Literal['program-change']
    program: Annotated[int, Field(strict=True, ge=1, le=128)]


def program_address(value: object) -> str:
    if isinstance(value, dict):
        return 'instrument' if 'instrument' in value else 'channel'
    return 'instrument' if isinstance(value, ProgramChange) else 'channel'


class Note(ChannelMessage, frozen=True):
    type: Literal['note-on', 'note-off']
    note: Annotated[int, Field(strict=True, ge=0, le=127)]
    velocity: Annotated[int, Field(strict=True, ge=0, le=127)]


class ChannelPressure(ChannelMessage, frozen=True):
    type: Literal['channel-pressure']
    pressure: Annotated[int, Field(strict=True, ge=0, le=127)]


class PolyphonicKeyPressure(ChannelMessage, frozen=True):
    type: Literal['polyphonic-key-pressure']
    note: Annotated[int, Field(strict=True, ge=0, le=127)]
    pressure: Annotated[int, Field(strict=True, ge=0, le=127)]


class TimeCodeQuarterFrame(OutputMessage, frozen=True):
    type: Literal['time-code-quarter-frame']
    piece: Annotated[int, Field(strict=True, ge=0, le=7)]
    value: Annotated[int, Field(strict=True, ge=0, le=15)]


class SongPositionPointer(OutputMessage, frozen=True):
    type: Literal['song-position-pointer']
    position: Annotated[int, Field(strict=True, ge=0, le=16383)]


class SongSelect(OutputMessage, frozen=True):
    type: Literal['song-select']
    song: Annotated[int, Field(strict=True, ge=0, le=127)]


class SystemMessage(OutputMessage, frozen=True):
    type: Literal[
        'tune-request',
        'timing-clock',
        'start',
        'continue',
        'stop',
        'active-sensing',
        'system-reset',
    ]


class Brightness(Presentation, frozen=True):
    operation: Literal['brightness']
    value: Annotated[int, Field(strict=True, ge=0, le=255)]


class UsbMidiInterface(Presentation, frozen=True):
    operation: Literal['usb-midi-interface']
    value: Annotated[bool, Field(strict=True)]


class Navigation(Presentation, frozen=True):
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
    patches: list['Patch']


class Reference(Presentation, frozen=True):
    patch: str
    document: str | None = None


class Derived(BaseModel, frozen=True):
    parent: str
    type: Literal['derived'] = 'derived'
    # Overrides are checked against the parent's concrete patch type at compile time.
    model_config = ConfigDict(extra='allow')


ConcretePatch = (
    Reference
    | Sequence
    | Annotated[
        Brightness | UsbMidiInterface | Navigation, Field(discriminator='operation')
    ]
    | Annotated[
        Annotated[
            Annotated[ProgramChange, Tag('instrument')]
            | Annotated[ChannelProgramChange, Tag('channel')],
            Discriminator(program_address),
        ]
        | ControlChange
        | PitchBend
        | Nrpn
        | SysEx
        | Note
        | ChannelPressure
        | PolyphonicKeyPressure
        | TimeCodeQuarterFrame
        | SongPositionPointer
        | SongSelect
        | SystemMessage
        | StockFirmware,
        Field(discriminator='type'),
    ]
)
Patch = Derived | ConcretePatch
Sequence.model_rebuild()
patch_adapter = TypeAdapter(Patch)


class TablePatch(BaseModel, frozen=True):
    instrument: str
    profile: str = 'default'
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
    profiles: list[InstrumentProfile]
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
    next: Window = Field(default_factory=Window)
    sent: Window = Field(default_factory=Window)
    undo: Window = Field(default_factory=Window)

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
    horizontal_scale: Annotated[
        float, Field(strict=True, gt=0, allow_inf_nan=False)
    ] = 1.0
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


class TargetPulse(BaseModel, frozen=True):
    delay: Annotated[
        float,
        Field(strict=True, ge=0, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 2.0
    period: Annotated[
        float,
        Field(strict=True, ge=0.002, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 4.0
    minimum: Annotated[int, Field(strict=True, ge=0, le=128)] = 64
    maximum: Annotated[int, Field(strict=True, ge=1, le=128)] = 128

    @model_validator(mode='after')
    def ordered_limits(self) -> 'TargetPulse':
        if self.minimum > self.maximum:
            raise ValueError('minimum must not exceed maximum')
        return self

    model_config = ConfigDict(extra='forbid')


class NavigationSettings(BaseModel, frozen=True):
    hold_delay: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 1.5
    repeat_interval: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 1.0

    model_config = ConfigDict(extra='forbid')


class UtilityPages(BaseModel, frozen=True):
    refresh_interval: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 1.0
    clock_timeout: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 1.0
    clocks_per_quarter: Annotated[int, Field(strict=True, ge=1, le=960)] = 24
    clock_average_beats: Annotated[int, Field(strict=True, ge=1, le=16)] = 1
    tap_intervals: Annotated[int, Field(strict=True, ge=1, le=8)] = 4
    minimum_taps: Annotated[int, Field(strict=True, ge=2, le=9)] = 2
    minimum_interval: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.1
    reset_gap: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 3.0
    flash_fraction: Annotated[
        float,
        Field(strict=True, ge=0.001, le=1, multiple_of=0.001),
        units.unit_validator('dimensionless'),
    ] = 0.4
    flasher_diameter: Annotated[
        int, Field(strict=True, ge=1, le=128), units.unit_validator('pixel')
    ] = 40
    annulus: Annotated[
        int, Field(strict=True, ge=1, le=64), units.unit_validator('pixel')
    ] = 5
    title_height: Annotated[
        int, Field(strict=True, ge=8, le=128), units.unit_validator('pixel')
    ] = 12
    tempo_height: Annotated[
        int, Field(strict=True, ge=8, le=128), units.unit_validator('pixel')
    ] = 40
    padding: Annotated[
        int, Field(strict=True, ge=0, le=64), units.unit_validator('pixel')
    ] = 2
    tempo_font: Literal['CourierPrime', 'CourierPrimeBold'] = 'CourierPrime'
    status_font: Literal['classic', 'Arial', 'ComicSansMS'] = 'classic'
    title_font: Literal['classic', 'Arial', 'ComicSansMS'] = 'classic'
    off_color: str = '0x600000'
    on_color: str = '0xFF0000'
    ring_color: str = 'black'
    background_color: str = '0xFFF085'
    digits: Annotated[int, Field(strict=True, ge=1, le=4)] = 3
    decimals: Annotated[int, Field(strict=True, ge=0, le=2)] = 1

    @model_validator(mode='after')
    def feasible_taps(self) -> 'UtilityPages':
        if self.minimum_taps > self.tap_intervals + 1:
            raise ValueError('minimum_taps exceeds tap_intervals + 1')
        if self.minimum_interval > self.reset_gap:
            raise ValueError('minimum_interval exceeds reset_gap')
        return self

    model_config = ConfigDict(extra='forbid')


class Setup(BaseModel, frozen=True):
    # Validate entries independently so one malformed patch cannot hide others.
    instruments: dict[str, object] = Field(default_factory=dict)
    patches: dict[str, object] = Field(default_factory=dict)
    patch_cycles: dict[str, object] = Field(default_factory=dict)
    definitions: Definitions | None = None
    fonts: dict[str, str] = Field(default_factory=dict)
    seed: Annotated[int, Field(strict=True)] | None = 0
    fill_in_styles: Annotated[bool, Field(strict=True)] = True
    background_color: str = '0xFFF8D8'
    foreground_brightness_limit: Annotated[
        float, Field(strict=True, gt=0, le=1), units.unit_validator('dimensionless')
    ] = 0.5
    target_pulse: TargetPulse = Field(default_factory=TargetPulse)
    navigation: NavigationSettings = Field(default_factory=NavigationSettings)
    utility_pages: UtilityPages = Field(default_factory=UtilityPages)
    take_first_match: Annotated[bool, Field(strict=True)] = False
    brightness: Annotated[int, Field(strict=True, ge=0, le=255)] = 192
    transition: (
        Annotated[
            float,
            Field(strict=True, ge=0.001, le=65.535),
            units.unit_validator('second'),
            AfterValidator(millisecond_resolution),
        ]
        | None
    ) = None
    splash: Annotated[bool, Field(strict=True)] = True
    animation_color_cycle: Annotated[bool, Field(strict=True)] = False
    is_midi_interface: Annotated[bool, Field(strict=True)] = False
    usb_device_midi: Annotated[bool, Field(strict=True)] = True
    usb_host_midi: Annotated[bool, Field(strict=True)] = True
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
    tags: Annotated[list[str], BeforeValidator(parse_tags)] = Field(
        default_factory=list
    )
    tag_styles: dict[str, Style] = Field(default_factory=dict)
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
