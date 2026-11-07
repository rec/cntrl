"""Host-side CoreMIDI packet access and target-independent transfer verification."""

import ctypes
from time import sleep


class Midi:
    def __init__(self) -> None:
        self.core = ctypes.CDLL(
            '/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI'
        )
        self.cf = ctypes.CDLL(
            '/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation'
        )
        self.cf.CFStringCreateWithCString.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_uint32,
        ]
        self.cf.CFStringCreateWithCString.restype = ctypes.c_void_p
        self.cf.CFStringGetCString.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_long,
            ctypes.c_uint32,
        ]
        self.cf.CFRelease.argtypes = [ctypes.c_void_p]
        self.core.MIDIObjectGetStringProperty.argtypes = [
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p),
        ]
        self.callback_type = ctypes.CFUNCTYPE(
            None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p
        )
        self.core.MIDIClientCreate.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.core.MIDIOutputPortCreate.argtypes = [
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.core.MIDIInputPortCreate.argtypes = [
            ctypes.c_uint32,
            ctypes.c_void_p,
            self.callback_type,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.core.MIDIPacketListInit.argtypes = [ctypes.c_void_p]
        self.core.MIDIPacketListInit.restype = ctypes.c_void_p
        self.core.MIDIPacketListAdd.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_void_p,
            ctypes.c_uint64,
            ctypes.c_ulong,
            ctypes.c_void_p,
        ]
        self.core.MIDIPacketListAdd.restype = ctypes.c_void_p
        self.core.MIDISend.argtypes = [
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_void_p,
        ]
        self.core.MIDIGetNumberOfSources.restype = ctypes.c_ulong
        self.core.MIDIGetNumberOfDestinations.restype = ctypes.c_ulong
        self.data = bytearray()
        self.client = ctypes.c_uint32()
        self.output = ctypes.c_uint32()
        self.input = ctypes.c_uint32()
        self.callback = self.callback_type(self.receive)
        name = self.string('cntrl MIDI qualification')
        try:
            self.check(
                self.core.MIDIClientCreate(name, None, None, ctypes.byref(self.client))
            )
            self.check(
                self.core.MIDIOutputPortCreate(
                    self.client, name, ctypes.byref(self.output)
                )
            )
            self.check(
                self.core.MIDIInputPortCreate(
                    self.client, name, self.callback, None, ctypes.byref(self.input)
                )
            )
        finally:
            self.cf.CFRelease(name)

    def string(self, value: str) -> int:
        return self.cf.CFStringCreateWithCString(None, value.encode(), 0x08000100)

    def name(self, endpoint: int) -> str:
        key = self.string('displayName')
        value = ctypes.c_void_p()
        try:
            self.check(
                self.core.MIDIObjectGetStringProperty(
                    endpoint, key, ctypes.byref(value)
                )
            )
            buffer = ctypes.create_string_buffer(1024)
            if not self.cf.CFStringGetCString(value, buffer, len(buffer), 0x08000100):
                raise ValueError('MIDI endpoint name exceeds buffer')
            return buffer.value.decode()
        finally:
            self.cf.CFRelease(key)
            if value.value:
                self.cf.CFRelease(value)

    def endpoints(self, source: bool) -> dict[str, int]:
        count = (
            self.core.MIDIGetNumberOfSources()
            if source
            else self.core.MIDIGetNumberOfDestinations()
        )
        result: dict[str, int] = {}
        print(f'CoreMIDI {"sources" if source else "destinations"}: {count}')
        for i in range(count):
            endpoint = (
                self.core.MIDIGetSource(i)
                if source
                else self.core.MIDIGetDestination(i)
            )
            name = self.name(endpoint)
            if name in result:
                raise ValueError(f'Duplicate CoreMIDI display name: {name}')
            result[name] = endpoint
        return result

    def receive(self, packets: int, context: int | None, source: int | None) -> None:
        count = ctypes.c_uint32.from_address(packets).value
        packet = (
            packets + 4
        )  # MIDIServices.h declares these structs packed to four bytes.
        for _ in range(count):
            length = ctypes.c_uint16.from_address(packet + 8).value
            self.data.extend(ctypes.string_at(packet + 10, length))
            packet = (packet + 10 + length + 3) & ~3

    def send(self, endpoint: int, data: bytes) -> None:
        buffer = ctypes.create_string_buffer(len(data) + 64)
        packet = self.core.MIDIPacketListInit(buffer)
        payload = ctypes.create_string_buffer(data)
        if not self.core.MIDIPacketListAdd(
            buffer, len(buffer), packet, 0, len(data), payload
        ):
            raise ValueError('MIDI packet allocation failed')
        self.check(self.core.MIDISend(self.output, endpoint, buffer))

    def close(self) -> None:
        self.core.MIDIClientDispose(self.client)

    @staticmethod
    def check(status: int) -> None:
        if status:
            raise OSError(status, 'CoreMIDI operation failed')


def exchange(
    midi: Midi,
    endpoint: int,
    payload: bytes,
    expected: bytes,
    *,
    chunk_bytes: int,
    interval_seconds: float,
    settle_seconds: float,
    label: str,
) -> bytes:
    """Send bounded chunks and verify the complete received byte stream."""
    if chunk_bytes <= 0:
        raise ValueError('MIDI chunk size must be positive')
    midi.data.clear()
    for offset in range(0, len(payload), chunk_bytes):
        midi.send(endpoint, payload[offset : offset + chunk_bytes])
        if interval_seconds:
            sleep(interval_seconds)
    if settle_seconds:
        sleep(settle_seconds)
    received = bytes(midi.data)
    if received != expected:
        raise ValueError(
            f'{label} mismatch: expected {len(expected)}, got {len(received)} bytes'
        )
    return received
