"""Build and communicate with the shared native desktop runtime."""

import os
import struct
import subprocess
import sys
import threading
import time
from collections.abc import Iterator
from contextlib import ExitStack
from functools import cache
from hashlib import sha256
from heapq import merge
from pathlib import Path
from queue import Empty, Full, Queue
from threading import Thread, Timer
from types import TracebackType
from typing import Literal

import tomlkit
from pydantic import BaseModel

from .. import crashes, models
from ..firmware import native_cache
from ..firmware.build_info import commit_id
from ..firmware.capabilities import header
from ..fonts.catalogue import FONT_IDS
from ..localization import catalogue
from ..patch_chooser.models import Target
from . import font_comparison
from .corrections import TranslationRegion
from .errors import EmulatorError, emulator_files
from .layout import EmulatorLayout
from .localization import TOOLTIP_PAGES
from .midi_history import MidiHistory
from .replay import Event, validate_name_key

MAX_REPLAY_ADVANCE_US = 10_000_000


@cache
def translation_message_ids() -> tuple[str, ...]:
    return tuple(catalogue.load().messages)


class RequestRejected(EmulatorError):
    """The native engine declined an input and remains available."""


class OutputEvent(BaseModel, frozen=True):
    kind: Literal['midi', 'error', 'relay', 'keyboard', 'firmware', 'memon-note']
    endpoint: str = ''
    data: bytes = b''
    time_us: int = 0
    message: str = ''
    value: int = 0


def bind_target(layout: EmulatorLayout, target: Target) -> models.Controller:
    controller = layout.hardware()
    if {c.id for c in controller.controls} != set(target.buttons):
        raise EmulatorError(
            'execution target inputs do not match catalogue control IDs'
        )
    if set(layout.displays) != {d.id for d in target.displays}:
        raise EmulatorError(
            'execution target displays do not match catalogue display IDs'
        )
    for control in controller.controls:
        if not isinstance(control, models.Button):
            raise EmulatorError(
                f'cntrl backend does not support built-in {control.kind} '
                f'control {control.id}'
            )
    for display in controller.displays:
        if display.kind != 'graphical':
            raise EmulatorError(
                f'cntrl backend does not support {display.kind} display {display.id}'
            )
    return controller


def build(target: Target, directory: Path, source_root: Path) -> Path:
    if (
        target.animation is not None
        and not target.animation_display_streams
        and len(target.animation.streams) != len(target.displays)
    ):
        raise EmulatorError(
            'emulator animations require a stream mapping for every display'
        )
    if target.animation is not None and any(
        d.width != target.animation.width or d.height != target.animation.height
        for d in target.displays
    ):
        raise EmulatorError('emulator animations require matching-size displays')
    with emulator_files():
        directory = directory.resolve()
        source_root = source_root.resolve()
        directory.mkdir(parents=True, exist_ok=True)
    include = source_root / 'firmware/include'
    source = source_root / 'firmware/emulator/main.cpp'
    with emulator_files():
        has_source = source.is_file()
    if not has_source:
        raise EmulatorError(
            'the emulator requires a cntrl source checkout; specify --source-root'
        )
    generated = {'target_capabilities.h': header(target)}
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
    with emulator_files():
        for name, text in generated.items():
            (directory / name).write_text(text)
        inputs = [
            source,
            *(
                p
                for d in (source.parent, include)
                for p in d.rglob('*')
                if p.suffix in ('.h', '.inc')
            ),
            *(directory / n for n in generated),
        ]
    if sys.platform == 'win32':
        inputs.append(source_root / 'scripts/windows-native.manifest')
    with emulator_files():
        key = native_cache.build_key(command, directory, inputs)
        cache = source_root / 'build/native-cache'
        record = directory / 'emulator.sha256'
        if not (
            executable.is_file() and record.is_file() and record.read_text() == key
        ):
            # A failed compiler may truncate an old executable. Never reuse its old key.
            record.write_text('')
            if not native_cache.restore(cache, key, executable):
                if commit := commit_id(source_root):
                    define = '/D' if sys.platform == 'win32' else '-D'
                    command.insert(1, f'{define}CNTRL_GIT_COMMIT=0x{commit}')
                subprocess.run(command, check=True)
                native_cache.store(cache, key, executable)
            record.write_text(key)
    revision = subprocess.run(
        [str(executable), '--build-gitref'], check=False, capture_output=True
    )
    crashes.native_emulator_ref = (
        revision.stdout.decode('ascii').strip().removeprefix('0x')
        if revision.returncode == 0
        else ''
    )
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
        with emulator_files():
            self.mode = (
                'pedal mirror'
                if (storage / 'mirror.toml').is_file()
                else 'setup preview'
            )
        self.controller = bind_target(layout, target)
        self.time_us = 0
        self.random_seed = 1
        self._recording = False
        self._recording_end_us: int | None = None
        self._midi_history: dict[int, MidiHistory] = {}
        self._other_events: list[tuple[int, Event]] = []
        self.failed = False
        self.stock_firmware = False
        self.doom = False
        self.editing_name = False
        self.language = 'en'
        self.correction_regions: list[TranslationRegion] = []
        self.correction_language_name = ''
        self.requested_correction_language = ''
        self.tooltip_context: tuple[str, tuple[tuple[str, int], ...]] = ('', ())
        self.held: set[str] = set()
        self.font_samples: list[font_comparison.FontSample] = []
        self.font_sample_index = 0
        self.font_sample_button = ''
        self.messages: list[OutputEvent] = []
        self.memory_usage: dict[str, int] = {}
        self.frames = {d.id: bytes(d.width * d.height * 2) for d in target.displays}
        with emulator_files():
            source_hash = sha256(setup.read_bytes()).hexdigest()
            source_record = storage / 'setup-source.sha256'
            command = [
                str(executable.resolve()),
                str(storage.resolve()),
                str(setup.resolve()),
                '0'
                if source_record.is_file() and source_record.read_text() == source_hash
                else '1',
            ]
        for name in dict.fromkeys(
            [*target.ui_fonts, *(n for n in target.font_sources if n in FONT_IDS)]
        ):
            if name not in FONT_IDS:
                raise EmulatorError(f'unknown UI font family: {name}')
            if name not in target.font_sources:
                raise EmulatorError(
                    f'execution target lacks exported UI font source: {name}'
                )
            command.extend(
                [str(FONT_IDS[name]), str(Path(target.font_sources[name]).resolve())]
            )
        self.pending = bytearray()
        self.output: Queue[bytes] = Queue(maxsize=16)
        self.reader_stop = threading.Event()
        self.reader_error: OSError | None = None
        self.reader = Thread(target=self.read_stdout, daemon=True)
        self.closed = False
        self._cleanup_complete = False
        with ExitStack() as startup:
            with emulator_files():
                self.process = subprocess.Popen(
                    command,
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                )
            startup.callback(self.close)
            self.reader.start()
            self.receive()
            if self.mode == 'pedal mirror':
                with emulator_files():
                    snapshot = (storage / 'status-snapshot.bin').read_bytes()
                self.request(b'\x0e' + snapshot)
            errors = [e.message for e in self.messages if e.kind == 'error']
            if errors:
                raise EmulatorError(
                    'native emulator startup failed: ' + '; '.join(errors)
                )
            with emulator_files():
                source_record.write_text(source_hash)
            startup.pop_all()

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
        if self._cleanup_complete:
            return
        self.closed = True
        self.reader_stop.set()
        with ExitStack() as cleanup:
            if self.process.stdout is not None:
                cleanup.callback(self.process.stdout.close)
            if self.reader.ident is not None:
                cleanup.callback(self.join_reader)
            cleanup.callback(self.stop_process)
            if self.process.stdin is not None:
                try:
                    self.process.stdin.close()
                except BrokenPipeError:
                    pass
        self._cleanup_complete = True

    def stop_process(self) -> None:
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)

    def join_reader(self) -> None:
        self.reader.join(timeout=3)
        if self.reader.is_alive():
            raise TimeoutError('native emulator reader did not stop')

    def poll_exit(self) -> int | None:
        return self.process.poll()

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
                raise EmulatorError('native emulator response timed out')
            try:
                part = self.output.get(timeout=remaining)
            except Empty:
                raise EmulatorError('native emulator response timed out') from None
            if not part:
                if self.reader_error is not None:
                    raise self.reader_error
                raise EmulatorError(
                    f'native emulator disconnected (exit {self.process.poll()})'
                )
            self.pending.extend(part)
        result = bytes(self.pending[:size])
        del self.pending[:size]
        return result

    def request(self, payload: bytes) -> None:
        if self.failed or self.closed:
            raise EmulatorError('native emulator session is no longer usable')
        if len(payload) > 65536:
            raise EmulatorError('emulator request exceeds 65536 bytes')
        assert self.process.stdin is not None
        watchdog = Timer(self.response_timeout, self.process.kill)
        watchdog.daemon = True
        watchdog.start()
        try:
            self.process.stdin.write(struct.pack('<I', len(payload)) + payload)
            self.process.stdin.flush()
            self.receive()
        except RequestRejected:
            raise
        except (OSError, ValueError):
            self.failed = True
            raise
        finally:
            watchdog.cancel()

    def capture_corrections(self, enabled: bool) -> None:
        self.request(bytes((18, enabled)))
        if not enabled:
            self.correction_regions = []
            self.correction_language_name = ''

    def preview_correction(self, region: TranslationRegion, replacement: str) -> None:
        self.request(
            struct.pack('<BHB', 17, region.index, region.field == 'short')
            + replacement.encode()
        )

    def clear_corrections(self) -> None:
        self.request(b'\x10')

    def resolve_correction_language(self, apply: bool) -> None:
        self.request(bytes((19, apply)))

    def receive(self) -> None:
        self.deadline = time.monotonic() + self.response_timeout
        updated: dict[str, bytearray] = {}
        regions: list[TranslationRegion] | None = None
        language_name = ''
        requested_language = ''
        rejection = None
        while True:
            (size,) = struct.unpack('<I', self.read(4))
            if not 1 <= size <= 65536:
                raise EmulatorError('invalid native emulator frame size')
            payload = self.read(size)
            if payload == b'\0':
                self.requested_correction_language = requested_language
                for display, pixels in updated.items():
                    self.frames[display] = bytes(pixels)
                if regions is not None:
                    self.correction_regions = regions
                    self.correction_language_name = language_name
                if rejection is not None:
                    raise RequestRejected(rejection)
                return
            if payload[0] == 1:
                self.rectangle(payload[1:], updated)
            elif payload[0] == 2 and len(payload) >= 10:
                timestamp, endpoint = struct.unpack_from('<QB', payload, 1)
                connections = {e.route: e.name for e in self.target.midi_endpoints}
                if endpoint not in connections:
                    raise EmulatorError(
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
                    raise EmulatorError('native emulator returned an unknown relay')
                self.messages.append(
                    OutputEvent(
                        kind='relay',
                        endpoint=name,
                        value=payload[1],
                        time_us=self.time_us,
                    )
                )
            elif payload[0] == 5 and len(payload) == 13:
                self.memory_usage = dict(
                    zip(
                        ('used', 'peak', 'failures'),
                        struct.unpack('<III', payload[1:]),
                        strict=True,
                    )
                )
            elif payload[0] == 6 and len(payload) == 17:
                self.messages.append(
                    OutputEvent(
                        kind='keyboard',
                        data=payload[9:],
                        time_us=struct.unpack_from('<Q', payload, 1)[0],
                        message=f'Simulated keyboard HID: {payload[9:].hex(" ")}',
                    )
                )
            elif payload[0] == 7 and len(payload) == 9:
                self.stock_firmware = True
                self.messages.append(
                    OutputEvent(
                        kind='firmware',
                        time_us=struct.unpack_from('<Q', payload, 1)[0],
                        message=(
                            'Simulated stock firmware selected; '
                            'restart the session to return to CNTRL.'
                        ),
                    )
                )
            elif payload[0] == 15 and len(payload) == 9:
                self.doom = True
                self.messages.append(
                    OutputEvent(
                        kind='firmware',
                        time_us=struct.unpack_from('<Q', payload, 1)[0],
                        message='Doom selected. Restart to return to CNTRL.',
                    )
                )
            elif payload[0] == 16 and len(payload) == 10 and payload[9] in (0, 1):
                firmware = 'stock firmware' if payload[9] else 'CNTRL'
                self.messages.append(
                    OutputEvent(
                        kind='firmware',
                        time_us=struct.unpack_from('<Q', payload, 1)[0],
                        message=f'Simulated boot default saved: {firmware}.',
                    )
                )
            elif payload[0] == 8 and len(payload) == 2 and payload[1] <= 1:
                self.editing_name = bool(payload[1])
            elif payload[0] == 9:
                self.language = payload[1:].decode()
            elif payload[0] == 10:
                rejection = payload[1:].decode()
            elif payload[0] == 11 and len(payload) == 2 + 2 * len(self.target.displays):
                if any(p >= len(TOOLTIP_PAGES) for p in payload[1:2] + payload[2::2]):
                    raise EmulatorError('unknown tooltip page')
                self.tooltip_context = (
                    TOOLTIP_PAGES[payload[1]],
                    tuple(
                        (TOOLTIP_PAGES[p], w)
                        for p, w in zip(payload[2::2], payload[3::2], strict=True)
                    ),
                )
            elif payload[0] == 12:
                regions = []
                language_name = payload[1:].decode()
            elif payload[0] == 13 and len(payload) >= 17 and regions is not None:
                screen, short, index, left, top, right, bottom, short_length = (
                    struct.unpack_from('<BBHHHHHH', payload, 1)
                )
                names = translation_message_ids()
                literal = payload[15]
                text_offset = 17 + payload[16] * 8
                if (
                    index >= len(names)
                    or screen >= len(self.target.displays)
                    or short > 1
                    or text_offset > len(payload)
                ):
                    raise EmulatorError('invalid native translation region')
                regions.append(
                    TranslationRegion(
                        message=names[index],
                        field='short' if short else 'text',
                        original=payload[text_offset:].decode(),
                        index=index,
                        screen=screen,
                        left=left,
                        top=top,
                        right=right,
                        bottom=bottom,
                        short_length=short_length,
                        literal=None if literal == 255 else literal,
                        glyphs=[
                            list(g)
                            for g in struct.iter_unpack(
                                '<HHHH', payload[17:text_offset]
                            )
                        ],
                    )
                )
            elif payload[0] == 14:
                requested_language = payload[1:].decode()
            elif payload[0] == 17 and len(payload) == 13 and payload[1] <= 1:
                self.messages.append(
                    OutputEvent(
                        kind='memon-note',
                        data=payload[2:5],
                        value=payload[1],
                        time_us=struct.unpack_from('<Q', payload, 5)[0],
                    )
                )
            else:
                raise EmulatorError('unknown native emulator response')

    def rectangle(self, payload: bytes, updated: dict[str, bytearray]) -> None:
        if len(payload) < 10:
            raise EmulatorError('truncated display rectangle')
        screen, x, y, width, height = struct.unpack_from('<5H', payload)
        if screen >= len(self.target.displays):
            raise EmulatorError('native emulator returned an unknown display')
        display = self.target.displays[screen]
        if (
            x + width > display.width
            or y + height > display.height
            or not width
            or not height
            or len(payload) != 10 + width * height * 2
        ):
            raise EmulatorError('invalid native display rectangle')
        if display.id not in updated:
            updated[display.id] = bytearray(self.frames[display.id])
        # Match the byte order on both sides by copying each RGB565 pixel as a unit.
        frame = memoryview(updated[display.id]).cast('H')
        pixels = memoryview(payload)[10:].cast('H')
        start = y * display.width + x
        if width == 1:
            frame[start : start + height * display.width : display.width] = pixels
            return
        # The device streams columns, while the host display expects rows.
        for row in range(height):
            frame[start : start + width] = pixels[row::height]
            start += display.width

    @property
    def firmware_exited(self) -> bool:
        return self.stock_firmware or self.doom

    def advance(self, time_us: int, *, replay: bool = False) -> None:
        if not self.time_us <= time_us <= 0xFFFFFFFFFFFFFFFF:
            raise EmulatorError('virtual time must advance monotonically within uint64')
        cursor = self.time_us
        self.time_us = time_us
        # Whole-millisecond segments preserve the native tick phase. Intermediate
        # acknowledgements give each segment its own watchdog without settling.
        while (
            replay
            and not self.firmware_exited
            and time_us - cursor > MAX_REPLAY_ADVANCE_US
        ):
            cursor += MAX_REPLAY_ADVANCE_US
            try:
                self.request(b'\x01' + struct.pack('<Q', cursor) + b'\x00')
            except RequestRejected as error:
                # Alternate --source-root checkouts may use the older wire format.
                if str(error) != 'unsupported or malformed emulator command':
                    raise
                break
        self.request(b'\x01' + struct.pack('<Q', time_us))

    def memory(self) -> dict[str, int]:
        """Read the checked heap, charged using the target allocator's alignment."""
        self.request(b'\x0c')
        return self.memory_usage

    @property
    def events(self) -> list[Event]:
        return list(self.iter_events())

    def record_replay(self, enabled: bool) -> None:
        if enabled == self._recording:
            return
        self._recording = enabled
        if enabled:
            self._midi_history = {
                e.route: MidiHistory() for e in self.target.midi_endpoints if e.input
            }
            self._other_events.clear()
            self._recording_end_us = None
        else:
            self._recording_end_us = self.time_us
            for history in self._midi_history.values():
                history.reset()

    def iter_events(self) -> Iterator[Event]:
        midi = (
            self._midi_history[e.route].events(e.name)
            for e in sorted(self.target.midi_endpoints, key=lambda e: e.route)
            if e.route in self._midi_history
        )
        for _, event in merge(*midi, self._other_events, key=lambda item: item[0]):
            yield event

    def write_replay(self, path: Path) -> None:
        with path.open('w', encoding='utf-8') as stream:
            stream.write(
                tomlkit.dumps(
                    {
                        'seed': self.random_seed,
                        'until': (
                            self.time_us
                            if self._recording_end_us is None
                            else self._recording_end_us
                        )
                        / 1_000_000,
                    }
                )
            )
            for event in self.iter_events():
                stream.write('\n')
                stream.write(tomlkit.dumps({'events': [event.model_dump(mode='json')]}))

    def _record_event(
        self, kind: str, *, target: str = '', value: int = 0, text: str = ''
    ) -> None:
        if self._recording:
            event = Event.model_validate(
                {
                    'at': self.time_us / 1_000_000,
                    'kind': kind,
                    'target': target,
                    'value': value,
                    'text': text,
                }
            )
            self._other_events.append((self.time_us, event))

    def key(self, key: int) -> None:
        validate_name_key(key)
        self.request(b'\x0f' + bytes([key]))
        self._record_event('key', value=key)

    def external_keyboard(self, connected: bool) -> None:
        self.request(b'\x14' + bytes([connected]))
        self._record_event('keyboard_connection', value=int(connected))

    def button(self, control_id: str, pressed: bool) -> None:
        if control_id not in self.target.buttons:
            raise EmulatorError(f'unknown button: {control_id}')
        was_held = control_id in self.held
        if pressed:
            self.held.add(control_id)
        else:
            self.held.discard(control_id)
        if self.font_samples and control_id == self.font_sample_button:
            if pressed and not was_held:
                self.font_sample_index = (self.font_sample_index + 1) % len(
                    self.font_samples
                )
                self.preview_font()
        else:
            self.request(bytes([2, self.target.buttons.index(control_id), pressed]))
        self._record_event('button', target=control_id, value=int(pressed))

    def compare_status_fonts(self) -> None:
        button = next(
            (c.id for c in self.controller.controls if c.name.upper() == 'F'), None
        )
        if button is None:
            raise EmulatorError('Status font comparison requires a switch labelled F')
        self.font_sample_button = button
        self.font_samples = font_comparison.load()
        self.font_sample_index = 0
        self.preview_font()

    def preview_font(self) -> None:
        sample = self.font_samples[self.font_sample_index]
        label = sample.name.encode('ascii')
        self.request(
            bytes([13, sample.size, len(label)])
            + label
            + (font_comparison.DIRECTORY / sample.file).read_bytes()
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
            raise EmulatorError(f'unknown MIDI input: {endpoint}')
        if not data:
            raise EmulatorError('MIDI input requires message bytes')
        for offset in range(0, len(data), 65534):
            chunk = data[offset : offset + 65534]
            self.request(bytes([3, connection.route]) + chunk)
            if self._recording:
                self._midi_history[connection.route].record(self.time_us, chunk)

    def block(self, endpoint: str, blocked: bool) -> None:
        connection = next(
            (e for e in self.target.midi_endpoints if e.name == endpoint and e.output),
            None,
        )
        if connection is None:
            raise EmulatorError(f'unknown MIDI output: {endpoint}')
        self.request(bytes([4, connection.route, blocked]))
        self._record_event('block', target=endpoint, value=int(blocked))

    def connection(self, endpoint: str, connected: bool) -> None:
        connection = next(
            (e for e in self.target.midi_endpoints if e.name == endpoint), None
        )
        if connection is None:
            raise EmulatorError(f'unknown MIDI endpoint: {endpoint}')
        self.request(bytes([10, connection.route, connected]))
        if self._recording and connection.input and not connected:
            self._midi_history[connection.route].reset()
        self._record_event('connection', target=endpoint, value=int(connected))

    def cleanup(self) -> None:
        self.release_all()
        self.request(b'\x09')
        self._record_event('cleanup')

    def expression(self, port: int, value: int) -> None:
        if not 0 <= port < 4 or not 0 <= value <= 1023:
            raise EmulatorError('expression requires a port 0..3 and raw value 0..1023')
        self.request(bytes([5, port]) + struct.pack('<H', value))
        self._record_event('expression', target=str(port), value=value)

    def text(self, message: str, warning: bool = False) -> None:
        self.request(bytes([6, 2 if warning else 1]) + message.encode('ascii'))
        self._record_event('text', text=message, value=int(warning))

    def auxiliary(self, port: int, contacts: int) -> None:
        if not 0 <= port < 4 or not 0 <= contacts <= 3:
            raise EmulatorError(
                'auxiliary input requires a port 0..3 and contact mask 0..3'
            )
        self.request(bytes([11, port, contacts]))
        self._record_event('auxiliary', target=str(port), value=contacts)

    def seed(self, value: int) -> None:
        self.request(b'\x07' + struct.pack('<I', value))
        self.random_seed = value

    def absolute(self, control_id: str, value: float) -> None:
        raise EmulatorError(
            f'cntrl backend does not support built-in absolute control {control_id}'
        )

    def relative(self, control_id: str, delta: int) -> None:
        raise EmulatorError(
            f'cntrl backend does not support built-in relative control {control_id}'
        )
