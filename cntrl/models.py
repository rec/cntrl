"""Controller descriptions, independent of firmware and editor implementations."""

from typing import Annotated, Literal, Self

from pydantic import BaseModel, Field, model_validator

from . import enums


class ValueRange(BaseModel, frozen=True):
    minimum: float = Field(allow_inf_nan=False)
    maximum: float = Field(allow_inf_nan=False)

    @model_validator(mode='after')
    def validate_order(self) -> Self:
        if self.minimum >= self.maximum:
            raise ValueError('minimum must be less than maximum')
        return self


class Button(BaseModel, frozen=True):
    """A control that reports press and release events."""

    id: str
    name: str
    kind: Literal[enums.ControlKind.BUTTON] = enums.ControlKind.BUTTON


class AbsoluteControl(BaseModel, frozen=True):
    """A potentiometer or pedal reporting a position in a bounded range."""

    id: str
    name: str
    value_range: ValueRange
    kind: Literal[enums.ControlKind.ABSOLUTE] = enums.ControlKind.ABSOLUTE


class RelativeControl(BaseModel, frozen=True):
    """An endless dial reporting direction/steps, not absolute position."""

    id: str
    name: str
    kind: Literal[enums.ControlKind.RELATIVE] = enums.ControlKind.RELATIVE


class Display(BaseModel, frozen=True):
    """Dimensions are character cells for text displays, pixels for graphical ones.

    Colours count independently selectable display colours, excluding the background.
    None means that a dimension or colour count is unknown.
    """

    id: str
    name: str
    kind: enums.DisplayKind
    width: int | None = Field(default=None, gt=0)
    height: int | None = Field(default=None, gt=0)
    colors: int | None = Field(default=None, ge=1)


class PhysicalSpecification(BaseModel, frozen=True):
    connector: enums.ConnectorKind
    contacts: int | None = Field(default=None, gt=0)
    diameter_mm: float | None = Field(default=None, gt=0, allow_inf_nan=False)


class MidiProtocol(BaseModel, frozen=True):
    kind: Literal[enums.ProtocolKind.MIDI] = enums.ProtocolKind.MIDI
    trs_wiring: enums.MidiWiring | None = None


class EthernetProtocol(BaseModel, frozen=True):
    """An Ethernet data connection, independent of application protocols."""

    kind: Literal[enums.ProtocolKind.ETHERNET] = enums.ProtocolKind.ETHERNET


class CvProtocol(BaseModel, frozen=True):
    voltage_range: ValueRange
    kind: Literal[enums.ProtocolKind.CV] = enums.ProtocolKind.CV


class ExpressionProtocol(BaseModel, frozen=True):
    """Analog expression-pedal sensing."""

    kind: Literal[enums.ProtocolKind.EXPRESSION] = enums.ProtocolKind.EXPRESSION


class SwitchProtocol(BaseModel, frozen=True):
    """External switch-contact sensing."""

    kind: Literal[enums.ProtocolKind.SWITCH] = enums.ProtocolKind.SWITCH


class RelayProtocol(BaseModel, frozen=True):
    """Relay-contact switching output, rather than switch-contact sensing."""

    kind: Literal[enums.ProtocolKind.RELAY] = enums.ProtocolKind.RELAY


class PortMode(BaseModel, frozen=True):
    """A supported electrical use, not an active setting or firmware feature."""

    direction: enums.Direction
    protocol: Annotated[
        MidiProtocol
        | EthernetProtocol
        | CvProtocol
        | ExpressionProtocol
        | SwitchProtocol
        | RelayProtocol,
        Field(discriminator='kind'),
    ]


class Port(BaseModel, frozen=True):
    """One physical port with fixed or configurable electrical uses."""

    id: str
    name: str
    physical: PhysicalSpecification
    modes: list[PortMode] = Field(min_length=1)

    @property
    def configurable(self) -> bool:
        return len(self.modes) > 1


class Controller(BaseModel, frozen=True):
    name: str
    controls: list[
        Annotated[
            Button | AbsoluteControl | RelativeControl, Field(discriminator='kind')
        ]
    ] = Field(default_factory=list)
    displays: list[Display] = Field(default_factory=list)
    ports: list[Port | enums.UsbSocket] = Field(default_factory=list)
