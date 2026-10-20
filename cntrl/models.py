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


class UsbProtocol(BaseModel, frozen=True):
    """A USB data connection; power-only connections are not modelled."""

    role: enums.UsbRole
    functions: list[enums.UsbFunction]
    kind: Literal[enums.ProtocolKind.USB] = enums.ProtocolKind.USB


class EthernetProtocol(BaseModel, frozen=True):
    """Higher-level protocol names, such as RTP-MIDI, describe the data service."""

    protocols: list[str] = Field(default_factory=list)
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


class PortConfiguration(BaseModel, frozen=True):
    id: str
    direction: enums.Direction
    protocol: Annotated[
        MidiProtocol
        | UsbProtocol
        | EthernetProtocol
        | CvProtocol
        | ExpressionProtocol
        | SwitchProtocol
        | RelayProtocol,
        Field(discriminator='kind'),
    ]


class Port(BaseModel, frozen=True):
    """One physical port with fixed or software-configurable signal meaning."""

    id: str
    name: str
    physical: PhysicalSpecification
    configurations: list[PortConfiguration] = Field(min_length=1)
    active_configuration: str

    @property
    def configurable(self) -> bool:
        return len(self.configurations) > 1

    @model_validator(mode='after')
    def validate_configurations(self) -> Self:
        ids = [c.id for c in self.configurations]
        if len(ids) != len(set(ids)):
            raise ValueError('port configuration IDs must be unique')
        if self.active_configuration not in ids:
            raise ValueError('active_configuration must name a supported configuration')
        return self


class Controller(BaseModel, frozen=True):
    name: str
    controls: list[
        Annotated[
            Button | AbsoluteControl | RelativeControl, Field(discriminator='kind')
        ]
    ] = Field(default_factory=list)
    displays: list[Display] = Field(default_factory=list)
    ports: list[Port] = Field(default_factory=list)
