"""Hardware-independent categories for controller capabilities."""

from enum import StrEnum, auto


class ControlKind(StrEnum):
    BUTTON = auto()
    ABSOLUTE = auto()
    RELATIVE = auto()


class DisplayKind(StrEnum):
    NUMERIC = auto()
    ALPHANUMERIC = auto()
    GRAPHICAL = auto()


class Direction(StrEnum):
    """Signal direction relative to the controller."""

    INPUT = auto()
    OUTPUT = auto()
    BIDIRECTIONAL = auto()


class ConnectorKind(StrEnum):
    DIN = auto()
    TS = auto()
    TRS = auto()
    RJ45 = auto()


class UsbSocket(StrEnum):
    USB_A = auto()
    USB_B = auto()
    USB_C = auto()
    USB_MINI_B = auto()
    USB_MICRO_B = auto()


class ProtocolKind(StrEnum):
    MIDI = auto()
    ETHERNET = auto()
    CV = auto()
    EXPRESSION = auto()
    SWITCH = auto()
    RELAY = auto()


class MidiWiring(StrEnum):
    """TRS MIDI wiring; active-only modes leave the other signal contact floating."""

    TYPE_A = auto()
    TYPE_B = auto()
    TIP_ACTIVE = auto()
    RING_ACTIVE = auto()
