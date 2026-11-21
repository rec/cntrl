"""Framed document transfer over a USB serial device."""

import secrets
import sys
import time
from hashlib import sha256
from pathlib import Path
from zlib import crc32

from pydantic import BaseModel, ValidationError
from serial import Serial, SerialTimeoutException

from . import binary_format, device_settings, global_overlay
from .data_files import InvalidSetupName, validate_name
from .models import PlayMidiFile, TargetMemory
from .usb_frames import HEADER, MAGIC, Frame, Kind, Status


class UsbProtocolError(ValueError):
    """The device replied, but this host cannot interpret its USB protocol."""


class UsbReplyError(ValueError):
    """The pedal rejected the command or returned an unexpected result."""


class UsbTransferInputError(ValueError):
    """Local input cannot be represented in a USB transfer."""


class UsbFileMissing(FileNotFoundError):
    """The pedal reported that the requested file does not exist."""


class UsbConnectionError(OSError):
    """The USB serial connection failed."""


class UsbSerialTimeout(TimeoutError):
    """A serial read or write timed out."""


class UsbCommandTimeout(TimeoutError):
    """The USB operation exceeded its deadline."""


class UsbCommandOutcomeUnknown(UsbCommandTimeout):
    """The operation timed out after command bytes may have reached the device."""


class SerialConnection:
    """A resumable, acknowledged byte stream over a replaceable serial port."""

    def __init__(self, port: Path, timeout: float = 30, *, retry: bool = True) -> None:
        self.port = port
        self.timeout = timeout
        self.retry = retry
        self.connection_attempted = False
        self.usb_block_size = 4096
        self.serial: Serial | None = None
        self.session = secrets.randbits(32) or 1
        self.sequence = 1
        self.acknowledged = 0
        self.received: list[Frame] = []
        self.frame_received = bytearray()
        self.bounded = not retry or not sys.stdin.isatty()
        self.deadline: float | None = None
        self.command_sent = False

    def __enter__(self) -> 'SerialConnection':
        try:
            self.exchange(b'', Kind.open)
        except (KeyboardInterrupt, OSError, UsbProtocolError):
            self.release()
            self.close()
            raise
        return self

    def __exit__(self, *args: object) -> None:
        try:
            if args and args[0] in (KeyboardInterrupt, UsbProtocolError):
                self.release()
            elif len(args) > 1 and isinstance(args[1], UsbCommandTimeout):
                if self.serial is not None:
                    self.serial.write_timeout = 0
                    self.release()
            elif args and args[0] is not None:
                if self.serial is not None:
                    try:
                        self.sequence = 0
                        self.exchange(b'', Kind.close, retry=False)
                    except (OSError, UsbProtocolError):
                        self.release()
            elif self.serial is not None:
                self.sequence = 0
                self.exchange(b'', Kind.close)
        finally:
            self.close()

    def release(self) -> None:
        if self.serial is not None:
            try:
                self.serial.write(
                    Frame(kind=Kind.close, session=self.session, sequence=0).encode()
                )
            except OSError:
                pass  # Cancellation must not wait for an unplugged device.

    def close(self) -> None:
        self.frame_received.clear()
        if self.serial is None:
            return
        port, self.serial = self.serial, None
        try:
            port.close()
        except OSError:
            pass  # The USB device may have disappeared during the command.

    def connect(self) -> None:
        try:
            self.serial = Serial(
                str(self.port),
                baudrate=115200,
                timeout=self.timeout,
                write_timeout=self.timeout,
            )
            self.serial.reset_input_buffer()
        except OSError as error:
            raise UsbConnectionError(str(error)) from error
        self.frame_received.clear()

    def exchange(
        self, data: bytes, kind: Kind = Kind.command, *, retry: bool = True
    ) -> None:
        if self.deadline is not None or not self.bounded:
            self._exchange(data, kind, retry)
            return
        self.deadline = time.monotonic() + self.timeout
        try:
            self._exchange(data, kind, retry)
        finally:
            self.deadline = None
            self.command_sent = False

    def _exchange(self, data: bytes, kind: Kind, retry: bool) -> None:
        frame = Frame(
            kind=kind,
            session=self.session,
            sequence=self.sequence,
            acknowledged=self.acknowledged,
            payload=data,
        ).encode()
        failures = 0
        delay = 1
        lost_reported = False
        while True:
            try:
                self._check_deadline()
                if self.serial is None:
                    if not retry or (not self.retry and self.connection_attempted):
                        raise UsbConnectionError('USB connection is closed')
                    self.connection_attempted = True
                    self.connect()
                self._check_deadline()
                self.command_sent |= bool(data)
                self.write_frame(frame)
                while True:
                    self._check_deadline()
                    response = self.read_frame()
                    self._check_deadline()
                    raw_response = response.encode()
                    if (
                        response.session != self.session
                        or response.sequence < self.sequence
                    ):
                        continue
                    if response.status == Status.lost:
                        if self.sequence == 0:
                            return
                        if not lost_reported:
                            print(
                                'USB session was lost; the unfinished command will not '
                                'be replayed. Hit control-C to exit.',
                                file=sys.stderr,
                                flush=True,
                            )
                            lost_reported = True
                        raise UsbConnectionError('USB session lost')
                    if response.status != Status.ok or response.kind not in (
                        Kind.text,
                        Kind.data,
                    ):
                        raise self.protocol_error(
                            f'Binary USB status={response.status.name}, '
                            f'kind={response.kind.name}',
                            raw_response,
                        )
                    if response.sequence != self.sequence:
                        raise self.protocol_error(
                            'Unexpected USB sequence', raw_response
                        )
                    if (
                        response.payload
                        and response.acknowledged == self.acknowledged + 1
                    ):
                        self.received.append(response)
                        self.acknowledged = response.acknowledged
                    elif (
                        response.payload and response.acknowledged != self.acknowledged
                    ):
                        raise self.protocol_error(
                            'Unexpected USB reply sequence', raw_response
                        )
                    self.usb_block_size = response.block_size
                    self.sequence += 1
                    return
            except (UsbConnectionError, UsbSerialTimeout) as error:
                self._check_deadline()
                self.close()
                if not retry or not self.retry:
                    raise
                failures += 1
                if failures >= 3:
                    print(
                        f'Waiting {delay} seconds for USB: {error} '
                        '(hit control-C to exit)',
                        file=sys.stderr,
                        flush=True,
                    )
                    # Short sleeps remain interruptible even with a very large
                    # accumulated backoff, without overflowing platform time_t.
                    for _ in range(delay):
                        self._pause(1)
                    delay *= 2

    def _check_deadline(self) -> None:
        if self.deadline is not None and time.monotonic() >= self.deadline:
            if self.command_sent:
                raise UsbCommandOutcomeUnknown(
                    'USB operation timed out after the command may have reached '
                    'the device. Check its state before retrying.'
                )
            raise UsbCommandTimeout('USB operation timed out')

    def _pause(self, seconds: float) -> None:
        self._check_deadline()
        if self.deadline is not None:
            seconds = min(seconds, max(0, self.deadline - time.monotonic()))
        time.sleep(seconds)

    def protocol_error(self, reason: str, response: bytes) -> UsbProtocolError:
        return UsbProtocolError(
            f'ERROR: {reason}\n'
            f'Port: {self.port}\n'
            f'Host transport: {__file__}\n'
            'Expected reply: binary CNTRL version 1, '
            '40-byte CRC-protected header and raw payload\n'
            f'Host state: session={self.session:x}, sequence={self.sequence}, '
            f'acknowledged_reply={self.acknowledged}, '
            f'usb_block_size={self.usb_block_size}\n'
            f'Received (length={len(response)}): {response!r}'
        )

    def write_frame(self, data: bytes) -> None:
        assert self.serial is not None
        deadline = (
            self.deadline
            if self.deadline is not None
            else time.monotonic() + self.timeout
        )
        pending = memoryview(data)
        while pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise UsbSerialTimeout('USB serial write timed out')
            try:
                self.serial.write_timeout = remaining
                count = self.serial.write(pending)
            except SerialTimeoutException as error:
                raise UsbSerialTimeout('USB serial write timed out') from error
            except OSError as error:
                raise UsbConnectionError(str(error)) from error
            if count == 0:
                raise UsbConnectionError('USB serial disconnected')
            pending = pending[count:]

    def read_frame(self) -> Frame:
        assert self.serial is not None
        deadline = (
            self.deadline
            if self.deadline is not None
            else time.monotonic() + self.timeout
        )
        discarded = bytearray()
        failure = (
            'expected a binary CNTRL frame; install matching binary-protocol firmware'
        )
        while True:
            limit = HEADER.size + max(1024, self.usb_block_size)
            if self.frame_received and not MAGIC.startswith(
                self.frame_received[: len(MAGIC)]
            ):
                discarded.extend(self.frame_received[:1])
                del self.frame_received[:1]
                if len(discarded) > limit:
                    raise self.protocol_error(failure, bytes(discarded))
                continue
            if len(self.frame_received) >= HEADER.size:
                header = bytes(self.frame_received[: HEADER.size])
                fields = HEADER.unpack(header)
                if crc32(header[:-4]) != fields[-1]:
                    failure = 'invalid binary USB header CRC'
                    discarded.extend(self.frame_received[:1])
                    del self.frame_received[:1]
                    continue
                if fields[1] != 1 or not 1 <= fields[7] <= 65535:
                    raise self.protocol_error(
                        f'unsupported USB version={fields[1]} '
                        f'or block_size={fields[7]}',
                        header,
                    )
                if fields[8] > max(1024, fields[7]):
                    raise self.protocol_error(
                        f'invalid binary USB payload length {fields[8]}', header
                    )
                limit = HEADER.size + fields[8]
                if len(self.frame_received) >= limit:
                    raw = bytes(self.frame_received[:limit])
                    if crc32(raw[HEADER.size :]) != fields[9]:
                        failure = 'invalid binary USB payload CRC'
                        discarded.extend(self.frame_received[:1])
                        del self.frame_received[:1]
                        continue
                    del self.frame_received[:limit]
                    try:
                        return Frame.decode(raw)
                    except ValueError as error:
                        raise self.protocol_error(str(error), raw) from error
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                if discarded or (self.sequence == 1 and self.deadline is None):
                    raise self.protocol_error(
                        failure, bytes(discarded + self.frame_received)
                    )
                raise UsbSerialTimeout('USB serial response timed out')
            try:
                available = self.serial.in_waiting
                if not available:
                    self.serial.timeout = remaining
                count = min(max(1, available), max(1, limit - len(self.frame_received)))
                value = self.serial.read(count)
            except OSError as error:
                raise UsbConnectionError(str(error)) from error
            if not value:
                if discarded or (self.sequence == 1 and self.deadline is None):
                    raise self.protocol_error(
                        failure, bytes(discarded + self.frame_received)
                    )
                raise UsbSerialTimeout('USB serial response timed out')
            self.frame_received.extend(value)

    def write(self, data: bytes, kind: Kind = Kind.command) -> None:
        if self.bounded and self.deadline is None:
            self.deadline = time.monotonic() + self.timeout
        for i in range(0, len(data), self.usb_block_size):
            self.exchange(data[i : i + self.usb_block_size], kind)

    def line(self, limit: int | None = None) -> str:
        if limit is None:
            limit = 1024
        if self.bounded and self.deadline is None:
            self.deadline = time.monotonic() + self.timeout
        try:
            while True:
                frame = self._reply()
                if frame.kind != Kind.text:
                    raise self.protocol_error(
                        'expected text reply, received binary data', frame.encode()
                    )
                line = frame.payload.rstrip(b'\r\n')
                if len(line) >= limit:
                    raise self.protocol_error(
                        f'USB serial response exceeds {limit - 1} bytes', frame.encode()
                    )
                response = self._reply_text(frame)
                if response != 'CNTRL WAIT':
                    return response
        finally:
            self.deadline = None
            self.command_sent = False

    def data(self) -> bytes:
        if self.bounded and self.deadline is None:
            self.deadline = time.monotonic() + self.timeout
        try:
            while True:
                frame = self._reply()
                if frame.kind == Kind.data:
                    return frame.payload
                text = self._reply_text(frame)
                if text != 'CNTRL WAIT':
                    raise UsbReplyError(text)
        finally:
            self.deadline = None
            self.command_sent = False

    def _reply(self) -> Frame:
        while True:
            self._check_deadline()
            if self.received:
                return self.received.pop(0)
            self.exchange(b'', Kind.poll)
            if not self.received:
                self._pause(0.01)

    def _reply_text(self, frame: Frame) -> str:
        try:
            return frame.payload.decode('ascii').rstrip('\r\n')
        except UnicodeDecodeError as error:
            raise self.protocol_error(
                'USB text reply is not ASCII', frame.encode()
            ) from error


class SetupEntry(BaseModel, frozen=True):
    name: str
    modified: int
    length: int
    checksum: int
    valid: bool = True


class SetupClient:
    def __init__(self, connection: SerialConnection) -> None:
        self.connection = connection

    def request(self, command: str) -> str:
        self.connection.write((command + '\n').encode('ascii'))
        response = self.connection.line()
        if response.startswith('CNTRL ERROR'):
            raise UsbReplyError(response)
        return response

    def ok(self, command: str) -> None:
        if (response := self.request(command)) != 'CNTRL OK':
            raise UsbReplyError(f'unexpected response: {response}')

    def send_data(self, kind: Kind, data: bytes) -> None:
        self.connection.write(data, kind)
        if (response := self.connection.line()) != 'CNTRL OK':
            raise UsbReplyError(response)

    def request_data(self, command: str) -> bytes:
        self.connection.write((command + '\n').encode('ascii'))
        return self.connection.data()

    def memory(self) -> TargetMemory:
        response = self.request('CNTRL MEMORY')
        fields = response.split()
        message = f'invalid memory response: {response}'
        if len(fields) != 4 or fields[:2] != ['CNTRL', 'MEMORY']:
            raise UsbReplyError(message)
        capacity = _reply_integer(fields[2], message)
        runtime = _reply_integer(fields[3], message)
        try:
            return TargetMemory(capacity=capacity, runtime=runtime)
        except ValidationError as error:
            raise UsbReplyError(f'{message}\n{error}') from error

    def global_settings(self) -> global_overlay.Overlay:
        fields = self.request('CNTRL GLOBAL GET').split()
        if fields[:2] != ['CNTRL', 'GLOBAL']:
            raise UsbReplyError('invalid device-global settings response')
        result: dict[int, int] = {}
        language: str | None = None
        startup_set: str | None = None
        for item in fields[2:]:
            key, separator, value = item.partition('=')
            if key == 'startup_set' and separator:
                if startup_set is not None:
                    raise UsbReplyError('duplicate startup set response')
                startup_set = _reply_hex_text(value, 'invalid startup set response')
                continue
            if key == 'language' and separator:
                if language is not None:
                    raise UsbReplyError('duplicate device-global language response')
                language = value
                continue
            if (
                not separator
                or not key.isascii()
                or not key.isdecimal()
                or not value.isascii()
                or not value.isdecimal()
            ):
                raise UsbReplyError('invalid device-global settings response')
            field = _reply_integer(key, 'invalid device-global settings response')
            raw = _reply_integer(value, 'invalid device-global settings response')
            if field in result:
                raise UsbReplyError('duplicate device-global settings response')
            result[field] = raw
        try:
            return global_overlay.Overlay.model_validate(
                {'values': result, 'language': language, 'startup_set': startup_set}
            )
        except ValidationError as error:
            raise UsbReplyError(
                f'invalid device-global settings response: {error}'
            ) from error

    def set_language(self, tag: str) -> None:
        try:
            global_overlay.Overlay.model_validate({'language': tag})
        except ValidationError as error:
            raise device_settings.DeviceSettingError(str(error)) from error
        self.ok(f'CNTRL GLOBAL SET language {tag}')

    def clear_language(self) -> None:
        self.ok('CNTRL GLOBAL CLEAR language')

    def set_global(self, field: int, value: int) -> None:
        device_settings.decode_value(field, value)
        self.ok(f'CNTRL GLOBAL SET {field} {value}')

    def clear_global(self, field: int) -> None:
        device_settings.definition(field)
        self.ok(f'CNTRL GLOBAL CLEAR {field}')

    def replace_globals(self, overlay: global_overlay.Overlay) -> None:
        suffix = ''.join(f' {k}={v}' for k, v in sorted(overlay.values.items()))
        if overlay.language is not None:
            suffix += f' language={overlay.language}'
        if overlay.startup_set is not None:
            suffix += f' startup_set={overlay.startup_set.encode().hex()}'
        self.ok('CNTRL GLOBAL PUT' + suffix)

    def catalogue(self) -> list[SetupEntry]:
        result: list[SetupEntry] = []
        response = self.request('CNTRL SETUP LIST')
        while response != 'CNTRL END':
            parts = response.split()
            invalid = len(parts) == 3 and parts[:2] == ['CNTRL', 'INVALID']
            message = f'invalid catalogue entry: {response}'
            if not invalid and (len(parts) != 6 or parts[:2] != ['CNTRL', 'ENTRY']):
                raise UsbReplyError(message)
            name = _reply_hex_text(parts[2], message)
            try:
                validate_name(name)
            except InvalidSetupName as error:
                raise UsbReplyError(message) from error
            modified, length, checksum = (
                (0, 0, 0)
                if invalid
                else (
                    _reply_integer(parts[3], message),
                    _reply_integer(parts[4], message),
                    _reply_integer(parts[5], message, 16),
                )
            )
            result.append(
                SetupEntry(
                    name=name,
                    modified=modified,
                    length=length,
                    checksum=checksum,
                    valid=not invalid,
                )
            )
            response = self.request('CNTRL NEXT')
        return result

    def current(self) -> str:
        response = self.request('CNTRL SETUP CURRENT')
        message = f'invalid selection response: {response}'
        if not response.startswith('CNTRL CURRENT '):
            raise UsbReplyError(message)
        return _reply_hex_text(response[14:], message)

    def select(self, name: str) -> None:
        self.ok('CNTRL SETUP SET ' + validate_name(name).encode('ascii').hex())

    def copy(self, name: str, data: bytes, overwrite: bool) -> None:
        if data.startswith(binary_format.MAGIC):
            binary_format.unpack(data, binary_format.Kind.setup)
            self.require_format(binary_format.Kind.setup)
        hex_name = validate_name(name).encode('ascii').hex()
        command = (
            f'CNTRL SETUP PUT {hex_name} {len(data)} {crc32(data):08x} {int(overwrite)}'
        )
        if (response := self.request(command)) != 'CNTRL SEND':
            raise UsbReplyError(response)
        self.connection.write(data, Kind.setup)
        if (response := self.connection.line()) != 'CNTRL OK':
            raise UsbReplyError(response)
        # Read back the committed bytes, not merely the acknowledged checksum.
        if self.download(name) != data:
            raise UsbReplyError(f'{name}: read-back verification failed')

    def copy_bundle(self, data: bytes, overwrite: bool) -> None:
        from .setup_bundle import unpack

        if data.startswith(binary_format.MAGIC):
            self.require_format(binary_format.Kind.bundle)
        if any(d.startswith(binary_format.MAGIC) for d in unpack(data).values()):
            self.require_format(binary_format.Kind.setup)
        response = self.request(
            f'CNTRL BUNDLE BEGIN {len(data)} {crc32(data):08x} {int(overwrite)}'
        )
        if response != 'CNTRL SEND':
            raise UsbReplyError(response)
        for i in range(0, len(data), self.connection.usb_block_size):
            self.send_data(Kind.bundle, data[i : i + self.connection.usb_block_size])
        self.ok('CNTRL BUNDLE END')
        if self.download_bundle() != data:
            raise UsbReplyError('bundle read-back verification failed')

    def require_format(self, kind: binary_format.Kind) -> None:
        response = self.request('CNTRL FORMAT').split()
        if (
            len(response) < 4
            or response[:3]
            != ['CNTRL', 'FORMAT', str(binary_format.DEVELOPMENT_FORMAT)]
            or str(int(kind)) not in response[3:]
        ):
            raise UsbReplyError(
                'firmware does not support the persistent document format'
            )

    def begin_deployment(self, *, recovery: bool = False) -> int:
        state = self.request('CNTRL DEPLOY STATUS').split()
        if (
            len(state) != 5
            or state[:2] != ['CNTRL', 'DEPLOY']
            or state[3] != '0'
            or state[4] not in (['0', '1'] if recovery else ['1'])
        ):
            raise UsbReplyError(
                'device requires deployment recovery before another update'
            )
        root = secrets.randbelow(0xFFFFFFFF) + 1
        self.ok(f'CNTRL DEPLOY BEGIN {root:08x}')
        return root

    def recover_deployment(self) -> bool:
        state = self.request('CNTRL DEPLOY STATUS').split()
        if (
            len(state) != 5
            or state[:2] != ['CNTRL', 'DEPLOY']
            or state[3] not in ('0', '1')
            or state[4] not in ('0', '1')
        ):
            raise UsbReplyError('firmware does not provide deployment recovery status')
        if state[3] == '1':
            response = self.request('CNTRL DEPLOY ABORT')
            if response not in ('CNTRL OK', 'CNTRL ERROR deployment-recovery'):
                raise UsbReplyError(response)
            state = self.request('CNTRL DEPLOY STATUS').split()
            if (
                len(state) != 5
                or state[:2] != ['CNTRL', 'DEPLOY']
                or state[3] != '0'
                or state[4] not in ('0', '1')
            ):
                raise UsbReplyError('deployment remains unresolved after abort')
        return state[4] == '1'

    def commit_deployment(self, bundle: bytes, library: bytes) -> None:
        self.ok(f'CNTRL DEPLOY COMMIT {crc32(bundle):08x} {crc32(library):08x}')

    def download_starts(self) -> bytes:
        response = self.request('CNTRL STARTS SIZE').split()
        if len(response) != 4 or response[:3] != ['CNTRL', 'STARTS', 'SIZE']:
            raise UsbReplyError('invalid startup counter size response')
        size = _reply_integer(response[3], 'invalid startup counter size')
        if not 0 < size <= 1024:
            raise UsbReplyError('invalid startup counter size')
        data = bytearray()
        while len(data) < size:
            part = self.request_data(f'CNTRL STARTS READ {len(data)}')
            if not 0 < len(part) <= size - len(data):
                raise UsbReplyError('invalid startup counter read')
            data.extend(part)
        return bytes(data)

    def download_state(self, path: str) -> bytes:
        encoded = path.encode('ascii').hex()
        response = self.request(f'CNTRL STATE SIZE {encoded}')
        if response == 'CNTRL STATE MISSING':
            raise UsbFileMissing(f'{path}: device state is missing')
        fields = response.split()
        if len(fields) != 3 or fields[:2] != ['CNTRL', 'STATE']:
            raise UsbReplyError(f'{path}: invalid device state size response')
        size = _reply_integer(fields[2], f'{path}: invalid device state size')
        if not 0 <= size <= 0xFFFFFFFF:
            raise UsbReplyError(f'{path}: invalid device state size')
        result = bytearray()
        while len(result) < size:
            part = self.request_data(f'CNTRL STATE READ {encoded} {len(result)}')
            if (
                not 0
                < len(part)
                <= min(self.connection.usb_block_size, size - len(result))
            ):
                raise UsbReplyError(f'{path}: invalid device state read')
            result.extend(part)
        return bytes(result)

    def download_bundle(self) -> bytes:
        parts = self.request('CNTRL BUNDLE SIZE').split()
        if len(parts) != 4 or parts[:2] != ['CNTRL', 'BUNDLE']:
            raise UsbReplyError('invalid bundle size response')
        length = _reply_integer(parts[2], 'invalid bundle size response')
        checksum = _reply_integer(parts[3], 'invalid bundle checksum response', 16)
        result = bytearray()
        for i in range(0, length, self.connection.usb_block_size):
            count = min(self.connection.usb_block_size, length - i)
            result.extend(self.request_data(f'CNTRL BUNDLE GET {i} {count}'))
        if len(result) != length or crc32(result) != checksum:
            raise UsbReplyError('bundle checksum mismatch')
        return bytes(result)

    def download(self, name: str, revision: int | None = None) -> bytes:
        hex_name = validate_name(name).encode('ascii').hex()
        response = self.request(
            ('CNTRL SETUP GET ' + hex_name)
            if revision is None
            else f'CNTRL HISTORY GET {hex_name} {revision}'
        )
        parts = response.split()
        message = f'invalid download response: {response}'
        if len(parts) != 4 or parts[:2] != ['CNTRL', 'FILE']:
            raise UsbReplyError(message)
        length = _reply_integer(parts[2], message)
        checksum = _reply_integer(parts[3], message, 16)
        if not 116 <= length <= 0xFFFFFFFF - 100:
            raise UsbReplyError('invalid download length')
        result = bytearray()
        while len(result) < length:
            chunk = self.request_data('CNTRL NEXT')
            if not 1 <= len(chunk) <= self.connection.usb_block_size:
                raise UsbReplyError('invalid download chunk')
            result.extend(chunk)
        if len(result) != length or crc32(result) != checksum:
            raise UsbReplyError('download checksum/length mismatch')
        return bytes(result)

    def delete(self, names: list[str]) -> None:
        payload = b''.join(validate_name(n).encode('ascii') + b'\0' for n in names)
        if not payload or len(payload) > 0xFFFFFFFF - 100:
            raise UsbTransferInputError(
                'deletion batch must fit a nonempty 32-bit storage record'
            )
        response = self.request(
            f'CNTRL SETUP DELETE {len(payload)} {crc32(payload):08x}'
        )
        if response != 'CNTRL SEND':
            raise UsbReplyError(response)
        self.connection.write(payload, Kind.setup)
        if (response := self.connection.line()) != 'CNTRL OK':
            raise UsbReplyError(response)

    def copy_midi(self, name: str, data: bytes, overwrite: bool) -> None:
        PlayMidiFile.basename(name)
        if (
            not 1 <= len(name) <= 95
            or name.lower().startswith('.cntrl-')
            or not 0 < len(data) <= 0xFFFFFFFF
        ):
            raise UsbTransferInputError('invalid MIDI file name or size')
        self._copy_asset('MIDI_FILE', Kind.midi_file, name, data, overwrite)

    def copy_source(self, snapshot: str, data: bytes) -> None:
        if sha256(data).hexdigest() != snapshot or not 0 < len(data) <= 0xFFFFFFFF:
            raise UsbTransferInputError('invalid source snapshot checksum or size')
        self._copy_asset('SOURCE', Kind.source, snapshot + '.bin', data, False)

    def copy_language(self, tag: str, data: bytes) -> None:
        from ..localization.catalogue import CatalogueError, inspect

        global_overlay.Overlay.model_validate({'language': tag})
        if inspect(data).language != tag:
            raise CatalogueError('language pack tag differs from its filename')
        self._copy_asset('LANGUAGE', Kind.language, tag + '.bin', data, True)

    def download_language(self, tag: str) -> bytes:
        from ..localization.catalogue import inspect

        global_overlay.Overlay.model_validate({'language': tag})
        data = self._download_asset('LANGUAGE', tag + '.bin')
        if inspect(data).language != tag:
            raise UsbReplyError('downloaded language pack tag differs')
        return data

    def download_source(self, snapshot: str) -> bytes:
        if len(snapshot) != 64 or any(c not in '0123456789abcdef' for c in snapshot):
            raise UsbTransferInputError('invalid source snapshot identifier')
        content = self._download_asset('SOURCE', snapshot + '.bin')
        if sha256(content).hexdigest() != snapshot:
            raise UsbReplyError('source archive checksum mismatch')
        return content

    def _copy_asset(
        self, protocol: str, kind: Kind, name: str, data: bytes, overwrite: bool
    ) -> None:
        label = {'MIDI_FILE': 'MIDI', 'LANGUAGE': 'language pack'}.get(
            protocol, 'source'
        )
        try:
            existing = self._download_asset(protocol, name)
        except UsbFileMissing:
            existing = None
        if existing == data:
            return
        if existing is not None and not overwrite:
            if protocol == 'SOURCE':
                raise UsbReplyError(
                    f'{name}: immutable source snapshot has corrupt contents'
                )
            raise UsbReplyError(
                f'{name}: {label} file exists with different contents; '
                'use --overwrite/-o'
            )
        encoded = name.encode('ascii').hex()
        response = self.request(
            f'CNTRL {protocol} BEGIN {encoded} {len(data)} '
            f'{crc32(data):08x} {int(overwrite)}'
        )
        if response != 'CNTRL SEND':
            raise UsbReplyError(response)
        for i in range(0, len(data), self.connection.usb_block_size):
            self.send_data(kind, data[i : i + self.connection.usb_block_size])
        response = self.request(f'CNTRL {protocol} END')
        if response == 'CNTRL OK CLEANUP_PENDING':
            print(
                f'{name}: {label} file committed, previous copy cleanup pending',
                file=sys.stderr,
            )
        elif response != 'CNTRL OK':
            raise UsbReplyError(f'unexpected response: {response}')
        if self._download_asset(protocol, name) != data:
            raise UsbReplyError(f'{name}: {label} file read-back mismatch')

    def download_midi(self, name: str) -> bytes:
        PlayMidiFile.basename(name)
        if not 1 <= len(name) <= 95 or name.lower().startswith('.cntrl-'):
            raise UsbTransferInputError('invalid MIDI file name')
        return self._download_asset('MIDI_FILE', name)

    def _download_asset(self, protocol: str, name: str) -> bytes:
        encoded = name.encode('ascii').hex()
        response = self.request(f'CNTRL {protocol} SIZE {encoded}')
        if response == f'CNTRL {protocol} MISSING':
            raise UsbFileMissing(f'{name}: {protocol} file is missing on the pedal')
        parts = response.split()
        message = f'invalid {protocol} file response: {response}'
        if len(parts) != 3 or parts[:2] != ['CNTRL', protocol]:
            raise UsbReplyError(message)
        length = _reply_integer(parts[2], message)
        if not 0 < length <= 0xFFFFFFFF:
            raise UsbReplyError(f'invalid {protocol} file length')
        result = bytearray()
        while len(result) < length:
            chunk = self.request_data(f'CNTRL {protocol} READ {encoded} {len(result)}')
            if (
                not 0
                < len(chunk)
                <= min(self.connection.usb_block_size, length - len(result))
            ):
                raise UsbReplyError(f'invalid {protocol} file chunk')
            result.extend(chunk)
        return bytes(result)

    def copy_font(self, key: int, data: bytes) -> None:
        if not 8 <= len(data) <= 262144 or not key or crc32(data) != key:
            raise UsbTransferInputError('invalid font asset size/checksum')
        response = self.request(f'CNTRL FONT BEGIN {key:08x} {len(data)}')
        if response not in ('CNTRL SEND', 'CNTRL EXISTS'):
            raise UsbReplyError(response)
        if response == 'CNTRL SEND':
            for i in range(0, len(data), self.connection.usb_block_size):
                self.send_data(Kind.font, data[i : i + self.connection.usb_block_size])
            self.ok('CNTRL FONT END')
        if self.download_font(key) != data:
            raise UsbReplyError('font asset read-back mismatch or checksum collision')

    def copy_movie(self, data: bytes) -> str:
        from ..animation.asset import inspect_asset

        inspect_asset(data)
        key = crc32(data)
        response = self.request(f'CNTRL SPLASH BEGIN {len(data)} {key:08x}')
        if response == 'CNTRL SPLASH SEND':
            for i in range(0, len(data), self.connection.usb_block_size):
                self.connection.write(
                    data[i : i + self.connection.usb_block_size], Kind.movie
                )
                received = min(i + self.connection.usb_block_size, len(data))
                expected = (
                    f'CNTRL SPLASH NEXT {received}'
                    if received < len(data)
                    else f'CNTRL SPLASH DONE {len(data)} {key:08x}'
                )
                if (response := self.connection.line()) != expected:
                    raise UsbReplyError(response)
        elif response != 'CNTRL SPLASH EXISTS':
            raise UsbReplyError(response)
        if self.download_movie(key) != data:
            raise UsbReplyError('movie asset read-back mismatch or checksum collision')
        return f'CNTRL SPLASH READY {len(data)} {key:08x}'

    def download_movie(self, key: int) -> bytes:
        from ..animation.asset import HEADER

        response = self.request(f'CNTRL SPLASH SIZE {key:08x}')
        if response == 'CNTRL SPLASH ERROR missing':
            raise UsbFileMissing(f'{key:08x}: device cannot read the stored movie')
        parts = response.split()
        if len(parts) != 4 or parts[:3] != ['CNTRL', 'SPLASH', 'SIZE']:
            raise UsbReplyError(f'invalid movie response: {response}')
        length = _reply_integer(parts[3], 'invalid movie asset length')
        if not HEADER.size < length <= 0xFFFFFFFF - 20:
            raise UsbReplyError('invalid movie asset length')
        result = bytearray()
        while len(result) < length:
            chunk = self.request_data(f'CNTRL SPLASH READ {key:08x} {len(result)}')
            if (
                not 1
                <= len(chunk)
                <= min(self.connection.usb_block_size, length - len(result))
            ):
                raise UsbReplyError('invalid movie asset chunk')
            result.extend(chunk)
        if crc32(result) != key:
            raise UsbReplyError('movie asset checksum mismatch')
        return bytes(result)

    def download_font(self, key: int) -> bytes:
        response = self.request(f'CNTRL FONT SIZE {key:08x}')
        parts = response.split()
        if len(parts) != 3 or parts[:2] != ['CNTRL', 'FONT']:
            raise UsbReplyError(f'invalid font response: {response}')
        length = _reply_integer(parts[2], 'invalid font asset length')
        if not 8 <= length <= 262144:
            raise UsbReplyError('invalid font asset length')
        result = bytearray()
        while len(result) < length:
            chunk = self.request_data(f'CNTRL FONT READ {key:08x} {len(result)}')
            if not 1 <= len(chunk) <= self.connection.usb_block_size:
                raise UsbReplyError('invalid font asset chunk')
            result.extend(chunk)
        if len(result) != length or crc32(result) != key:
            raise UsbReplyError('font asset checksum/length mismatch')
        return bytes(result)


def device_status(port: Path, reset_starts: bool = False) -> str:
    with SerialConnection(port) as connection:
        client = SetupClient(connection)
        if reset_starts:
            client.ok('CNTRL STARTS RESET')
        return client.request('CNTRL STATUS')


def _reply_integer(text: str, message: str, base: int = 10) -> int:
    try:
        return int(text, base)
    except ValueError as error:
        raise UsbReplyError(message) from error


def _reply_hex_text(text: str, message: str) -> str:
    try:
        return bytes.fromhex(text).decode('ascii')
    except ValueError as error:
        raise UsbReplyError(message) from error


USB_ERRORS = (
    UsbCommandTimeout,
    UsbConnectionError,
    UsbFileMissing,
    UsbProtocolError,
    UsbReplyError,
    UsbSerialTimeout,
)
