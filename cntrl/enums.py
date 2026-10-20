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
    USB_A = auto()
    USB_B = auto()
    USB_C = auto()
    USB_MINI_B = auto()
    USB_MICRO_B = auto()
    RJ45 = auto()


class ProtocolKind(StrEnum):
    MIDI = auto()
    USB = auto()
    ETHERNET = auto()
    CV = auto()
    EXPRESSION = auto()
    SWITCH = auto()


class MidiWiring(StrEnum):
    TYPE_A = auto()
    TYPE_B = auto()


class UsbRole(StrEnum):
    HOST = auto()
    DEVICE = auto()
    DUAL = auto()


class UsbFunction(StrEnum):
    MIDI = auto()
    HID = auto()
