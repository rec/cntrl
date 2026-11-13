"""Build and communicate with the shared native desktop runtime."""

import os
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path
from queue import Empty, Full, Queue
from threading import Thread, Timer
from types import TracebackType
from typing import Literal

from pydantic import BaseModel

from .. import models
from ..firmware.capabilities import header
from ..fonts.ui import embedded_header
from ..patch_switcher.models import Target
from .layout import EmulatorLayout
from .replay import Event


class OutputEvent(BaseModel, frozen=True):
    kind: Literal['midi', 'error', 'relay']
    endpoint: str = ''
    data: bytes = b''
    time_us: int = 0
    message: str = ''
    value: int = 0


def bind_target(layout: EmulatorLayout, target: Target) -> models.Controller:
    controller = layout.hardware()
    if {c.id for c in controller.controls} != set(target.buttons):
        raise ValueError('execution target inputs do not match catalogue control IDs')
    if set(layout.displays) != {d.id for d in target.displays}:
        raise ValueError('execution target displays do not match catalogue display IDs')
    for control in controller.controls:
        if not isinstance(control, models.Button):
            raise ValueError(
                f'cntrl backend does not support built-in {control.kind} '
                f'control {control.id}'
            )
    for display in controller.displays:
        if display.kind != 'graphical':
            raise ValueError(
                f'cntrl backend does not support {display.kind} display {display.id}'
            )
    return controller


def build(target: Target, directory: Path, source_root: Path) -> Path:
    if target.animation is not None and (
        len(target.animation.streams) != len(target.displays)
        or any(
            d.width != target.animation.width or d.height != target.animation.height
            for d in target.displays
        )
    ):
        raise ValueError(
            'emulator animations require one matching-size stream per display'
        )
    directory.mkdir(parents=True, exist_ok=True)
    include = source_root / 'firmware/include'
    source = source_root / 'firmware/emulator/main.cpp'
    if not source.is_file():
        raise ValueError(
            'the emulator requires a cntrl source checkout; specify --source-root'
        )
    fonts = {}
    for name in target.ui_fonts:
        if name not in target.font_sources:
            raise ValueError(f'execution target lacks exported UI font source: {name}')
        fonts[name] = Path(target.font_sources[name]).read_bytes()
    (directory / 'target_capabilities.h').write_text(header(target))
    (directory / 'emulator_fonts.h').write_text(embedded_header(fonts))
    executable = directory / ('emulator.exe' if sys.platform == 'win32' else 'emulator')
    if sys.platform == 'win32':
        command = [
            os.environ.get('CXX', 'cl'),
            '/nologo',
            '/std:c++17',
            '/EHsc',
            '/W4',
            '/WX',
            '/D_CRT_SECURE_NO_WARNINGS',
            '/DNOMINMAX',
            f'/I{include}',
            f'/I{directory}',
            str(source),
            f'/Fe{executable}',
            f'/Fo{directory / "emulator.obj"}',
            '/link',
            '/MANIFEST:EMBED',
            f'/MANIFESTINPUT:{source_root / "scripts/windows-native.manifest"}',
        ]
    else:
        command = [
            os.environ.get('CXX', 'c++'),
            '-std=c++17',
            '-O1',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-pedantic',
            f'-I{include}',
            f'-I{directory}',
            str(source),
            '-o',
            str(executable),
        ]
    subprocess.run(command, check=True)
    return executable


class Session:
    response_timeout = 10.0

    def __init__(
        self,
        layout: EmulatorLayout,
        target: Target,
        executable: Path,
        storage: Path,
        setup: Path,
    ) -> None:
        self.layout = layout
        self.target = target
        self.controller = bind_target(layout, target)
        self.time_us = 0
        self.random_seed = 1
        self.events: list[Event] = []
        self.failed = False
        self.held: set[str] = set()
        self.messages: list[OutputEvent] = []
        self.frames = {d.id: bytes(d.width * d.height * 2) for d in target.displays}
        self.process = subprocess.Popen(
            [str(executable.resolve()), str(storage.resolve()), str(setup.resolve())],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )
        self.pending = bytearray()
        self.output: Queue[bytes] = Queue(maxsize=16)
        self.reader_stop = threading.Event()
        self.reader_error: OSError | None = None
        self.reader = Thread(target=self.read_stdout, daemon=True)
        self.reader.start()
        self.closed = False
        try:
            self.receive()
            errors = [e.message for e in self.messages if e.kind == 'error']
            if errors:
                raise ValueError('native emulator startup failed: ' + '; '.join(errors))
        except (OSError, ValueError):
            self.close()
            raise

    def __enter__(self) -> 'Session':
        return self

    def __exit__(
        self,
        kind: type[BaseException] | None,
        value: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        if kind is None and not self.failed and self.process.poll() is None:
            try:
                self.cleanup()
            finally:
                self.close()
        else:
            self.close()

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        self.reader_stop.set()
        if self.process.stdin is not None:
            try:
                self.process.stdin.close()
            except BrokenPipeError:
                pass
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        if self.process.stdout is not None:
            self.process.stdout.close()
        self.reader.join(timeout=3)

    def read_stdout(self) -> None:
        assert self.process.stdout is not None
        pending: bytes | None = None
        while not self.reader_stop.is_set():
            if pending is None:
                try:
                    pending = os.read(self.process.stdout.fileno(), 65536)
                except OSError as error:
                    self.reader_error = error
                    pending = b''
            try:
                self.output.put(pending, timeout=0.1)
            except Full:
                continue
            if not pending:
                return
            pending = None

    def read(self, size: int) -> bytes:
        while len(self.pending) < size:
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                raise ValueError('native emulator response timed out')
            try:
                part = self.output.get(timeout=remaining)
            except Empty:
                raise ValueError('native emulator response timed out') from None
            if not part:
                if self.reader_error is not None:
                    raise self.reader_error
                raise ValueError(
                    f'native emulator disconnected (exit {self.process.poll()})'
                )
            self.pending.extend(part)
        result = bytes(self.pending[:size])
        del self.pending[:size]
        return result

    def request(self, payload: bytes) -> None:
        if self.failed or self.closed:
            raise ValueError('native emulator session is no longer usable')
        if len(payload) > 65536:
            raise ValueError('emulator request exceeds 65536 bytes')
        assert self.process.stdin is not None
        watchdog = Timer(self.response_timeout, self.process.kill)
        watchdog.daemon = True
        watchdog.start()
        try:
            self.process.stdin.write(struct.pack('<I', len(payload)) + payload)
            self.process.stdin.flush()
            self.receive()
        except (OSError, ValueError):
            self.failed = True
            raise
        finally:
            watchdog.cancel()

    def receive(self) -> None:
        self.deadline = time.monotonic() + self.response_timeout
        while True:
            (size,) = struct.unpack('<I', self.read(4))
            if not 1 <= size <= 65536:
                raise ValueError('invalid native emulator frame size')
            payload = self.read(size)
            if payload == b'\0':
                return
            if payload[0] == 1:
                self.rectangle(payload[1:])
            elif payload[0] == 2 and len(payload) >= 10:
                timestamp, endpoint = struct.unpack_from('<QB', payload, 1)
                connections = {e.route: e.name for e in self.target.midi_endpoints}
                if endpoint not in connections:
                    raise ValueError(
                        'native emulator returned an unknown MIDI endpoint'
                    )
                self.messages.append(
                    OutputEvent(
                        kind='midi',
                        endpoint=connections[endpoint],
                        data=payload[10:],
                        time_us=timestamp,
                    )
                )
            elif payload[0] == 3:
                self.messages.append(
                    OutputEvent(
                        kind='error', message=payload[1:].decode(), time_us=self.time_us
                    )
                )
            elif payload[0] == 4 and len(payload) >= 3 and payload[1] in (0, 1):
                name = payload[2:].decode()
                if name not in self.target.relay_outputs:
                    raise ValueError('native emulator returned an unknown relay')
                self.messages.append(
                    OutputEvent(
                        kind='relay',
                        endpoint=name,
                        value=payload[1],
                        time_us=self.time_us,
                    )
                )
            else:
                raise ValueError('unknown native emulator response')

    def rectangle(self, payload: bytes) -> None:
        if len(payload) < 10:
            raise ValueError('truncated display rectangle')
        screen, x, y, width, height = struct.unpack_from('<5H', payload)
        if screen >= len(self.target.displays):
            raise ValueError('native emulator returned an unknown display')
        display = self.target.displays[screen]
        if (
            x + width > display.width
            or y + height > display.height
            or not width
            or not height
            or len(payload) != 10 + width * height * 2
        ):
            raise ValueError('invalid native display rectangle')
        frame = bytearray(self.frames[display.id])
        for col in range(width):
            for row in range(height):
                source = 10 + 2 * (col * height + row)
                destination = 2 * ((y + row) * display.width + x + col)
                frame[destination : destination + 2] = payload[source : source + 2]
        self.frames[display.id] = bytes(frame)

    def advance(self, time_us: int) -> None:
        if not self.time_us <= time_us <= 0xFFFFFFFFFFFFFFFF:
            raise ValueError('virtual time must advance monotonically within uint64')
        self.time_us = time_us
        self.request(b'\x01' + struct.pack('<Q', time_us))

    def button(self, control_id: str, pressed: bool) -> None:
        if control_id not in self.target.buttons:
            raise ValueError(f'unknown button: {control_id}')
        if pressed:
            self.held.add(control_id)
        else:
            self.held.discard(control_id)
        self.request(bytes([2, self.target.buttons.index(control_id), pressed]))
        self.events.append(
            Event(
                at=self.time_us / 1000000,
                kind='button',
                target=control_id,
                value=int(pressed),
            )
        )

    def release_all(self) -> None:
        for identity in sorted(self.held):
            self.button(identity, False)

    def midi(self, endpoint: str, data: bytes) -> None:
        connection = next(
            (e for e in self.target.midi_endpoints if e.name == endpoint and e.input),
            None,
        )
        if connection is None:
            raise ValueError(f'unknown MIDI input: {endpoint}')
        if not data:
            raise ValueError('MIDI input requires message bytes')
        for offset in range(0, len(data), 65534):
            self.request(bytes([3, connection.route]) + data[offset : offset + 65534])
        self.events.append(
            Event(
                at=self.time_us / 1000000, kind='midi', target=endpoint, data=list(data)
            )
        )

    def block(self, endpoint: str, blocked: bool) -> None:
        connection = next(
            (e for e in self.target.midi_endpoints if e.name == endpoint and e.output),
            None,
        )
        if connection is None:
            raise ValueError(f'unknown MIDI output: {endpoint}')
        self.request(bytes([4, connection.route, blocked]))
        self.events.append(
            Event(
                at=self.time_us / 1000000,
                kind='block',
                target=endpoint,
                value=int(blocked),
            )
        )

    def connection(self, endpoint: str, connected: bool) -> None:
        connection = next(
            (e for e in self.target.midi_endpoints if e.name == endpoint), None
        )
        if connection is None:
            raise ValueError(f'unknown MIDI endpoint: {endpoint}')
        self.request(bytes([10, connection.route, connected]))
        self.events.append(
            Event(
                at=self.time_us / 1000000,
                kind='connection',
                target=endpoint,
                value=int(connected),
            )
        )

    def cleanup(self) -> None:
        self.release_all()
        self.request(b'\x09')
        self.events.append(Event(at=self.time_us / 1000000, kind='cleanup'))

    def expression(self, port: int, value: int) -> None:
        if not 0 <= port < 4 or not 0 <= value <= 1023:
            raise ValueError('expression requires a port 0..3 and raw value 0..1023')
        self.request(bytes([5, port]) + struct.pack('<H', value))
        self.events.append(
            Event(
                at=self.time_us / 1000000,
                kind='expression',
                target=str(port),
                value=value,
            )
        )

    def text(self, message: str, warning: bool = False) -> None:
        self.request(bytes([6, 2 if warning else 1]) + message.encode('ascii'))
        self.events.append(
            Event(
                at=self.time_us / 1000000, kind='text', text=message, value=int(warning)
            )
        )

    def auxiliary(self, port: int, contacts: int) -> None:
        if not 0 <= port < 4 or not 0 <= contacts <= 3:
            raise ValueError(
                'auxiliary input requires a port 0..3 and contact mask 0..3'
            )
        self.request(bytes([11, port, contacts]))
        self.events.append(
            Event(
                at=self.time_us / 1000000,
                kind='auxiliary',
                target=str(port),
                value=contacts,
            )
        )

    def seed(self, value: int) -> None:
        self.request(b'\x07' + struct.pack('<I', value))
        self.random_seed = value

    def absolute(self, control_id: str, value: float) -> None:
        raise ValueError(
            f'cntrl backend does not support built-in absolute control {control_id}'
        )

    def relative(self, control_id: str, delta: int) -> None:
        raise ValueError(
            f'cntrl backend does not support built-in relative control {control_id}'
        )
