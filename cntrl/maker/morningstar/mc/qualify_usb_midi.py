"""Explicit, macOS-only qualification of the MC USB-to-DIN MIDI path."""

import ctypes
import sys
import time
from pathlib import Path

import tyro
from pydantic import BaseModel, ConfigDict

from ....patch_switcher.transport import SerialConnection, SetupClient


class Options(BaseModel, frozen=True):
    """List endpoints by default; --run sends test traffic to the chosen output."""

    run: bool = False
    destination: str = ''
    source: str = ''
    port: Path = Path('/dev/cu.usbmodem144682801')
    expect_enabled: bool = False
    stress_seconds: float = 0

    model_config = ConfigDict(extra='forbid')


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


def main() -> None:
    options = tyro.cli(Options)
    if sys.platform != 'darwin':
        sys.exit('This qualification uses macOS CoreMIDI.')
    midi = Midi()
    try:
        sources, destinations = midi.endpoints(True), midi.endpoints(False)
        for n in sources:
            print(f'Input: {n}')
        for n in destinations:
            print(f'Output: {n}')
        if not options.run:
            return
        destination, source = destinations[options.destination], sources[options.source]
        midi.check(midi.core.MIDIPortConnectSource(midi.input, source, None))
        with SerialConnection(options.port, timeout=3) as connection:
            client = SetupClient(connection)
            before = client.request('CNTRL MIDI STATUS 0')
            print(before, flush=True)
            if before.split()[2] != str(int(options.expect_enabled)):
                raise ValueError(
                    'Bridge enabled state differs from requested qualification'
                )
            client.ok('CNTRL MIDI RESET')
            time.sleep(0.1)
            midi.data.clear()
            # Program changes and benign modulation controls, not note events.
            messages = b''.join(bytes((0xC0 | i % 16, i)) for i in range(128))
            messages += b''.join(bytes((0xB0 | i, 1, 0)) for i in range(16))
            midi.send(destination, messages)
            time.sleep(0.5)
            expected = messages if options.expect_enabled else b''
            if bytes(midi.data) != expected:
                raise ValueError(
                    f'Short-message mismatch: expected {len(expected)}, '
                    f'got {len(midi.data)} bytes'
                )
            print(f'Short messages: {len(midi.data)} bytes verified', flush=True)
            midi.data.clear()
            # Non-commercial SysEx; stream slowly enough for conventional MIDI.
            sysex = b'\xf0\x7d' + bytes(i % 128 for i in range(4096)) + b'\xf7'
            for i in range(0, len(sysex), 60):
                midi.send(destination, sysex[i : i + 60])
                time.sleep(0.025)
            time.sleep(0.2)
            expected = sysex if options.expect_enabled else b''
            if bytes(midi.data) != expected:
                raise ValueError(
                    f'SysEx mismatch: expected {len(expected)}, '
                    f'got {len(midi.data)} bytes'
                )
            print(f'Streamed SysEx: {len(midi.data)} bytes verified', flush=True)
            if options.stress_seconds:
                peers = [
                    v for k, v in destinations.items() if 'cntrl experimental' in k
                ]
                deadline = time.monotonic() + options.stress_seconds
                calls = 0
                while time.monotonic() < deadline:
                    for p in peers:
                        midi.send(p, messages)
                    calls += 1
                    time.sleep(0.001)
                time.sleep(0.5)
                print(f'Overload: {calls} bursts to {len(peers)} outputs', flush=True)
            for i in range(9):
                print(client.request(f'CNTRL MIDI STATUS {i}'), flush=True)
    finally:
        midi.close()


if __name__ == '__main__':
    main()
