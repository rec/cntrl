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

from ..animation.asset import Geometry
from ..fonts.catalogue import UI_FONTS


def millisecond_resolution(value: float) -> float:
    if value != round(value * 1000) / 1000:
        raise ValueError('duration must be a whole number of milliseconds')
    return value


def parse_tags(value: object) -> object:
    if isinstance(value, str):
        return [t.strip() for t in re.split('[,:]', value) if t.strip()]
    return value


def parse_movies(value: object) -> object:
    if isinstance(value, str):
        return value.split(':') if value else []
    return value


def infer_patch_type(value: object) -> object:
    if isinstance(value, str):
        return {'patch': value}
    if not isinstance(value, dict) or any(
        k in value
        for k in ('type', 'parent', 'patch', 'patches', 'operation', 'duration')
    ):
        return value
    if 'bindings' in value and not any(
        k in value for k in (*IMPLIED_MIDI_TYPES, 'note', 'value')
    ):
        return value | {'patches': []}
    types = {t for k, t in IMPLIED_MIDI_TYPES.items() if k in value}
    if 'note' in value:
        types.add('polyphonic-key-pressure' if 'pressure' in value else 'note-on')
        types.discard('channel-pressure')
    if not types and 'value' in value:
        types.add('pitch-bend')
    if len(types) > 1:
        raise ValueError('ambiguous patch fields; specify type explicitly')
    if types:
        return value | {'type': next(iter(types))}
    return value


def parse_binding(value: object) -> 'Patch | SwitchBinding | ExpressionAssignment':
    if isinstance(value, (SwitchBinding, ExpressionAssignment)):
        return value
    if isinstance(value, dict):
        if 'on' in value:
            return SwitchBinding.model_validate(value)
        if (
            'controller' in value
            and 'value' not in value
            and not any(
                k in value
                for k in ('type', 'parent', 'patch', 'patches', 'operation', 'duration')
            )
        ):
            return ExpressionAssignment.model_validate(value)
    return patch_adapter.validate_python(value)


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
    bindings: dict[str, object] | None = None

    @field_validator('bindings')
    @classmethod
    def control_bindings(
        cls, value: dict[str, object] | None
    ) -> dict[str, object] | None:
        if value is None:
            return None
        return {k: parse_binding(v) for k, v in value.items()}

    model_config = ConfigDict(extra='forbid')


class ProgramChange(Presentation, frozen=True):
    type: Literal['program-change']
    instrument: str | None = None
    program: Annotated[int, Field(strict=True, ge=1, le=128)] | StrictStr
    volume: Annotated[int, Field(strict=True, ge=0, le=127)] | None = None
    pan: Annotated[int, Field(strict=True, ge=-64, le=63)] | None = None
    bank: Annotated[int, Field(strict=True, ge=0)] | None = None


class OutputMessage(Presentation, frozen=True):
    type: str
    output: str | None = None


class ChannelMessage(OutputMessage, frozen=True):
    channel: Annotated[int, Field(strict=True, ge=1, le=16)] | None = None


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
        return 'channel' if 'output' in value or 'channel' in value else 'instrument'
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
        'next-song-patch',
    ]


class Shift(Presentation, frozen=True):
    operation: Literal['shift', 'unshift']


class PlayMidiFile(Presentation, frozen=True):
    operation: Literal['play-midi-file']
    filename: Annotated[str, Field(max_length=95)] = ''
    outputs: Annotated[list[str], Field(min_length=1)] | None = None
    loops: Annotated[int, Field(strict=True, ge=0, le=4294967295)] = 1
    pre_delay: Annotated[
        int, Field(strict=True, ge=0, le=4294967295), units.unit_validator('second')
    ] = 0
    post_delay: Annotated[
        int, Field(strict=True, ge=0, le=4294967295), units.unit_validator('second')
    ] = 0
    speed: Annotated[float, Field(gt=0, allow_inf_nan=False)] = 1.0
    reverse: bool = False
    transpose: Annotated[int, Field(strict=True, ge=-127, le=127)] = 0

    @field_validator('filename')
    @classmethod
    def basename(cls, value: str) -> str:
        if value in ('.', '..') or any(
            c in '/\\' or not 32 <= ord(c) <= 126 for c in value
        ):
            raise ValueError('filename must be an ASCII basename')
        return value

    @field_validator('outputs')
    @classmethod
    def unique_outputs(cls, value: list[str] | None) -> list[str] | None:
        if value is not None and len(value) != len(set(value)):
            raise ValueError('duplicate MIDI output')
        return value


class RecordMidiFile(Presentation, frozen=True):
    operation: Literal['record-midi-file']
    channels: Annotated[
        list[Annotated[int, Field(strict=True, ge=1, le=16)]], Field(min_length=1)
    ] = Field(default_factory=lambda: list(range(1, 17)))
    inputs: Annotated[list[str], Field(min_length=1)] | None = None

    @model_validator(mode='after')
    def unique_filters(self) -> 'RecordMidiFile':
        if len(self.channels) != len(set(self.channels)) or (
            self.inputs is not None and len(self.inputs) != len(set(self.inputs))
        ):
            raise ValueError('duplicate MIDI recording filter')
        return self


class StockFirmware(Presentation, frozen=True):
    operation: Literal['stock-firmware']
    exit_buttons: str | None = None

    @field_validator('exit_buttons')
    @classmethod
    def button_pair(cls, value: str | None) -> str | None:
        if value is None:
            return None
        buttons = value.split('+')
        if (
            len(buttons) != 2
            or any(
                not b or len(b) > 31 or any(not 32 <= ord(c) <= 126 for c in b)
                for b in buttons
            )
            or buttons[0] == buttons[1]
        ):
            raise ValueError(
                'exit_buttons must be two distinct input identifiers joined by +'
            )
        return '+'.join(sorted(buttons))


class Sequence(Presentation, frozen=True):
    patches: list['Patch']


class Reference(Presentation, frozen=True):
    patch: str
    document: str | None = None


class Delay(Presentation, frozen=True):
    type: Literal['delay'] = 'delay'
    duration: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ]


class Derived(BaseModel, frozen=True):
    parent: str
    type: Literal['derived'] = 'derived'
    # Overrides are checked against the parent's concrete patch type at compile time.
    model_config = ConfigDict(extra='allow')


ConcretePatch = (
    Delay
    | Reference
    | Sequence
    | Annotated[
        Brightness
        | UsbMidiInterface
        | Navigation
        | Shift
        | StockFirmware
        | PlayMidiFile
        | RecordMidiFile,
        Field(discriminator='operation'),
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
        | SystemMessage,
        Field(discriminator='type'),
    ]
)
Patch = Annotated[Derived | ConcretePatch, BeforeValidator(infer_patch_type)]


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


class PatchView(BaseModel, frozen=True):
    fields: list[str] = Field(
        default_factory=lambda: ['patch_cycle.name', 'display_name']
    )
    style: Style | None = None
    line_styles: dict[str, Style] = Field(default_factory=dict)

    @model_validator(mode='after')
    def unique_fields(self) -> 'PatchView':
        if len(self.fields) != len(set(self.fields)):
            raise ValueError('duplicate page field')
        if any(n not in self.fields for n in self.line_styles):
            raise ValueError('line_styles must refer to fields on this page')
        return self

    model_config = ConfigDict(extra='forbid')


class Window(BaseModel, frozen=True):
    view: str = 'patch'
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


class SwitchGestures(BaseModel, frozen=True):
    release: Patch | None = None
    long_press: Patch | None = None
    long_release: Patch | None = None
    double_tap: Patch | None = None
    double_release: Patch | None = None
    repeat: Patch | None = None
    debounce: Annotated[
        float,
        Field(strict=True, ge=0.001, le=1),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.02
    long_delay: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.5
    double_delay: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.3
    repeat_interval: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.1

    @model_validator(mode='after')
    def paired_events(self) -> 'SwitchGestures':
        if (
            self.long_release is not None
            and self.long_press is None
            and self.repeat is None
        ):
            raise ValueError('long_release requires long_press or repeat')
        if self.double_release is not None and self.double_tap is None:
            raise ValueError('double_release requires double_tap')
        if (
            min(self.long_delay, self.double_delay, self.repeat_interval)
            < self.debounce
        ):
            raise ValueError('gesture timings must be at least debounce')
        return self

    model_config = ConfigDict(extra='forbid')


class SwitchEnter(BaseModel, frozen=True):
    action: Patch
    override: Annotated[bool, Field(strict=True)] = False

    model_config = ConfigDict(extra='forbid')


class ShiftedActions(BaseModel, frozen=True):
    on: Patch
    off: Patch | None = None
    gestures: SwitchGestures | None = None

    model_config = ConfigDict(extra='forbid')


class SwitchBinding(BaseModel, frozen=True):
    name: str | None = None
    mode: Literal['press', 'momentary', 'latched'] = 'press'
    on: Patch
    off: Patch | None = None
    gestures: SwitchGestures | None = None
    enter: SwitchEnter | None = None
    exit: Patch | None = None
    shifted: ShiftedActions | None = None

    @model_validator(mode='after')
    def cleanup(self) -> 'SwitchBinding':
        for actions in (self, self.shifted):
            if actions is None:
                continue
            if actions.gestures is not None and self.mode != 'press':
                raise ValueError('gestures require press mode')
            if self.mode == 'press' and actions.off is not None:
                raise ValueError('press switches forbid off')
            if self.mode != 'press' and actions.off is None:
                raise ValueError('momentary and latched switches require off')
        return self

    model_config = ConfigDict(extra='forbid')


class SwitchAssignment(SwitchBinding, frozen=True):
    button: Annotated[str, Field(min_length=1, max_length=31)]
    style: Style | None = None


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


class ExpressionAssignment(BaseModel, frozen=True):
    output: str
    channel: Annotated[int, Field(strict=True, ge=1, le=16)]
    controller: Annotated[int, Field(strict=True, ge=0, le=127)]
    minimum: Annotated[int, Field(strict=True, ge=0, le=127)] = 0
    maximum: Annotated[int, Field(strict=True, ge=0, le=127)] = 127
    reverse: bool = Field(default=False, strict=True)
    curve: Literal['linear', 'slow', 'fast'] = 'linear'

    @model_validator(mode='after')
    def ordered_range(self) -> 'ExpressionAssignment':
        if self.maximum < self.minimum:
            raise ValueError(
                'minimum exceeds maximum; use reverse for toe-to-heel control'
            )
        return self

    model_config = ConfigDict(extra='forbid')


class KeyboardShortcut(BaseModel, frozen=True):
    name: Annotated[str, Field(min_length=1, max_length=31, pattern=r'^[ -~]+$')]
    keys: Annotated[
        list[Annotated[int, Field(strict=True, ge=4, le=115)]],
        Field(min_length=1, max_length=6),
    ]
    modifiers: Annotated[int, Field(strict=True, ge=0, le=255)] = 0

    model_config = ConfigDict(extra='forbid')


class PageSettings(BaseModel, frozen=True):
    keyboard: list[KeyboardShortcut] = Field(default_factory=list)
    expression: dict[str, ExpressionAssignment] = Field(default_factory=dict)
    clock_outputs: list[str] = Field(default_factory=list)
    clock_bpms: Annotated[
        list[Annotated[int, Field(strict=True, ge=20, le=600)]],
        Field(min_length=1, max_length=4),
    ] = Field(default_factory=lambda: [120])
    clock_source: Literal['internal', 'external'] = 'internal'
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
    tempo_font: str = 'CourierPrime'
    status_font: str = 'AtkinsonNextSemiBold'
    title_font: str = 'AtkinsonNextSemiBold'

    @field_validator('tempo_font', 'status_font', 'title_font')
    @classmethod
    def canonical_ui_font(cls, value: str) -> str:
        if value not in UI_FONTS:
            raise ValueError('unknown canonical UI font family')
        return value

    off_color: str = '0x600000'
    on_color: str = '0xFF0000'
    ring_color: str = 'black'
    background_color: str = '0xFFF085'
    digits: Annotated[int, Field(strict=True, ge=1, le=4)] = 3
    decimals: Annotated[int, Field(strict=True, ge=0, le=2)] = 1

    @model_validator(mode='after')
    def feasible_taps(self) -> 'PageSettings':
        if self.minimum_taps > self.tap_intervals + 1:
            raise ValueError('minimum_taps exceeds tap_intervals + 1')
        if self.minimum_interval > self.reset_gap:
            raise ValueError('minimum_interval exceeds reset_gap')
        return self

    model_config = ConfigDict(extra='forbid')


TrsMode = Literal['off', 'midi-a', 'midi-b', 'expression', 'aux']


class Scrolling(BaseModel, frozen=True):
    horizontal_duration: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.075
    vertical_duration: Annotated[
        float,
        Field(strict=True, ge=0.001, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.2
    send: Literal['right', 'left', 'up', 'down'] = 'right'
    patches: Literal['right', 'left', 'up', 'down'] = 'right'
    cycles: Literal['right', 'left', 'up', 'down'] = 'right'

    model_config = ConfigDict(extra='forbid')


class Page(BaseModel, frozen=True):
    type: Literal[
        'patch-switcher',
        'tap-tempo',
        'status',
        'firmware',
        'cable-test',
        'panic',
        'trs-ports',
        'expression',
        'clock-transport',
        'clock-tempo',
        'keyboard',
        'None',
    ] = 'patch-switcher'
    ram2_bytes: units.Bytes = Field(default=0, ge=0, le=4294967295)
    windows: Annotated[int | None, Field(strict=True, ge=0, le=255)] = None
    give_up_windows: list[str] = Field(default_factory=list, max_length=255)

    @model_validator(mode='after')
    def window_allocation(self) -> 'Page':
        if len(set(self.give_up_windows)) != len(self.give_up_windows):
            raise ValueError('give_up_windows must contain each window once')
        if self.type == 'None' and self.windows not in (None, 0):
            raise ValueError('None must have zero windows')
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
    inline_patches: Annotated[bool, Field(strict=True)] = True
    background_color: str = '0xFFF8D8'
    foreground_brightness_limit: Annotated[
        float, Field(strict=True, gt=0, le=1), units.unit_validator('dimensionless')
    ] = 0.5
    target_pulse: TargetPulse = Field(default_factory=TargetPulse)
    navigation: NavigationSettings = Field(default_factory=NavigationSettings)
    page_settings: PageSettings = Field(default_factory=PageSettings)
    pages: dict[str, Page] = Field(
        default_factory=lambda: {
            **{
                t: Page(type=t)
                for t in (
                    'status',
                    'firmware',
                    'cable-test',
                    'panic',
                    'trs-ports',
                    'expression',
                    'clock-transport',
                    'clock-tempo',
                    'keyboard',
                    'tap-tempo',
                )
            },
            'patch-switcher': Page(),
        },
        max_length=36,
    )
    take_first_match: Annotated[bool, Field(strict=True)] = False
    fast_switches: Annotated[bool, Field(strict=True)] = True
    brightness: Annotated[int, Field(strict=True, ge=0, le=255)] = 192
    scrolling: Scrolling = Field(default_factory=Scrolling)
    splash_movie: Annotated[
        list[Annotated[str, Field(strict=True, min_length=1)]],
        BeforeValidator(parse_movies),
        Field(strict=True, max_length=65535),
    ] = Field(default_factory=list)
    idle_movie: Annotated[
        list[Annotated[str, Field(strict=True, min_length=1)]],
        BeforeValidator(parse_movies),
        Field(strict=True, max_length=65535),
    ] = Field(default_factory=list)
    animation_color_cycle: Annotated[bool, Field(strict=True)] = False
    trs_ports: dict[str, TrsMode] = Field(default_factory=dict)
    auxiliary_switches: dict[str, Literal['independent', 'combined']] = Field(
        default_factory=dict
    )
    is_midi_interface: Annotated[bool, Field(strict=True)] = False
    usb_device_midi: Annotated[bool, Field(strict=True)] = True
    usb_host_midi: Annotated[bool, Field(strict=True)] = True
    forward_device_to_host: Annotated[bool, Field(strict=True)] = True
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
    patch_views: dict[str, PatchView] = Field(
        default_factory=lambda: {'patch': PatchView()}
    )
    tags: Annotated[list[str], BeforeValidator(parse_tags)] = Field(
        default_factory=list
    )
    tag_styles: dict[str, Style] = Field(default_factory=dict)
    parameters: dict[str, Parameter] = Field(default_factory=dict)
    switches: dict[str, object] = Field(default_factory=dict)

    model_config = ConfigDict(extra='forbid')


class TargetControls(BaseModel, frozen=True):
    previous: str | None = None
    next: str | None = None
    confirm: str | None = None
    previous_section: str | None = None
    next_section: str | None = None
    back: str | None = None
    next_page: str | None = None
    reverse_page: str | None = None
    tap: str | None = None
    app_actions: dict[
        str,
        Literal[
            'previous-patch',
            'next-patch',
            'activate-selection',
            'previous-group',
            'next-group',
            'undo',
        ],
    ] = Field(default_factory=dict)

    model_config = ConfigDict(extra='forbid')


class TargetMemory(BaseModel, frozen=True):
    capacity: units.Bytes = Field(gt=0, le=4294967295)
    runtime: units.Bytes = Field(ge=0, le=4294967295)

    @model_validator(mode='after')
    def available(self) -> 'TargetMemory':
        if self.runtime >= self.capacity:
            raise ValueError('runtime memory use exhausts target memory')
        return self

    model_config = ConfigDict(extra='forbid')


class TargetDisplay(BaseModel, frozen=True):
    id: str = Field(min_length=1, max_length=31, pattern=r'^[\x20-\x7e]+$')
    label: str
    width: units.Pixels = Field(gt=0, le=65535)
    height: units.Pixels = Field(gt=0, le=65535)
    gap: units.Pixels = Field(default=0, ge=0, le=65535)
    corner_radius: units.Pixels = Field(default=0, ge=0, le=65535)

    model_config = ConfigDict(extra='forbid')


class MidiEndpoint(BaseModel, frozen=True):
    name: Annotated[str, Field(min_length=1, max_length=31)]
    label: Annotated[str, Field(min_length=1, max_length=31)]
    route: Annotated[int, Field(strict=True, ge=0, le=15)]
    transport: Literal['physical', 'usb-device', 'usb-host'] = 'physical'
    input: bool = False
    output: bool = True
    port: Annotated[int, Field(strict=True, ge=0, le=3)] | None = None

    @field_validator('name', 'label')
    @classmethod
    def ascii_text(cls, value: str) -> str:
        if any(not 32 <= ord(c) <= 126 for c in value):
            raise ValueError('MIDI endpoint names and labels must be printable ASCII')
        return value


class Target(BaseModel, frozen=True):
    memory: TargetMemory
    functions: list[str] = Field(default_factory=list)
    buttons: list[Annotated[str, Field(min_length=1, max_length=31)]] = Field(
        default_factory=list, max_length=8
    )

    @field_validator('buttons')
    @classmethod
    def input_identifiers(cls, value: list[str]) -> list[str]:
        if len(value) != len(set(value)) or any(
            '+' in b or any(not 32 <= ord(c) <= 126 for c in b) for b in value
        ):
            raise ValueError(
                'input identifiers must be unique printable ASCII without +'
            )
        return value

    controls: TargetControls = Field(default_factory=TargetControls)
    scan_interval: Annotated[
        float,
        Field(gt=0, le=65.535),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.001
    usb_block_size: units.Bytes = Field(default=4096, ge=1, le=65535)
    max_switches: Annotated[int, Field(strict=True, ge=0, le=5)] = 5
    displays: list[TargetDisplay] = Field(min_length=1, max_length=255)
    fonts: list[FontFace]
    ui_fonts: list[str]

    @field_validator('ui_fonts')
    @classmethod
    def ui_font_families(cls, value: list[str]) -> list[str]:
        if 'classic' not in value:
            raise ValueError(
                'classic UI font is required for management and error screens'
            )
        if len(value) != len(set(value)) or any(n not in UI_FONTS for n in value):
            raise ValueError('UI fonts must name distinct canonical font families')
        return value

    font_sources: dict[str, str] = Field(default_factory=dict)
    animation: Geometry | None = None
    midi_endpoints: list[MidiEndpoint] = Field(max_length=16)
    default_midi_output: str
    trs_ports: dict[str, list[TrsMode]] = Field(default_factory=dict)
    max_instruments: int = Field(gt=0, le=32)
    max_patch_cycles: int = Field(gt=0, le=128)
    max_patches: int = Field(gt=0, le=65535)
    history_depth: int = Field(gt=0)
    characters: str

    @property
    def outputs(self) -> list[str]:
        return [e.name for e in self.midi_endpoints if e.output]

    @model_validator(mode='after')
    def unique_midi_endpoints(self) -> 'Target':
        if len({e.name for e in self.midi_endpoints}) != len(
            self.midi_endpoints
        ) or len({e.route for e in self.midi_endpoints}) != len(self.midi_endpoints):
            raise ValueError('MIDI endpoint names and routes must be unique')
        if sorted(e.route for e in self.midi_endpoints) != list(
            range(len(self.midi_endpoints))
        ):
            raise ValueError(
                'MIDI routes must cover 0 through endpoint count minus one'
            )
        outputs = {e.name for e in self.midi_endpoints if e.output}
        if self.default_midi_output not in outputs:
            raise ValueError('MIDI outputs must refer to declared output endpoints')
        return self

    @property
    def display_width(self) -> int:
        return min(d.width for d in self.displays)

    @property
    def display_height(self) -> int:
        return min(d.height for d in self.displays)

    @model_validator(mode='after')
    def distinct_displays(self) -> 'Target':
        if len({d.id for d in self.displays}) != len(self.displays):
            raise ValueError('display IDs must be unique')
        return self

    @model_validator(mode='after')
    def declared_controls(self) -> 'Target':
        bindings = self.controls.model_dump(exclude_none=True, exclude={'app_actions'})
        if (set(bindings.values()) | set(self.controls.app_actions)) - set(
            self.buttons
        ):
            raise ValueError('control bindings must refer to declared input IDs')
        return self

    model_config = ConfigDict(extra='forbid')


IMPLIED_MIDI_TYPES = {
    'program': 'program-change',
    'controller': 'control-change',
    'parameter': 'nrpn',
    'data': 'sysex',
    'pressure': 'channel-pressure',
    'piece': 'time-code-quarter-frame',
    'position': 'song-position-pointer',
    'song': 'song-select',
}


Sequence.model_rebuild()
patch_adapter = TypeAdapter(Patch)
