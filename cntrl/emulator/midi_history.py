"""Compact replay history for one MIDI 1.0 input stream."""

from array import array
from collections.abc import Iterator

from .replay import Event


class MidiHistory:
    def __init__(self) -> None:
        self._data = bytearray()
        self._times = array('Q')
        self._pending = bytearray()
        self._status = 0

    def reset(self) -> None:
        self._pending.clear()
        self._status = 0

    def record(self, time_us: int, data: bytes) -> None:
        for byte in data:
            if byte >= 0xF8:
                # Real-time bytes can arrive inside any other message.
                self._data.append(byte)
                self._times.append(time_us)
            elif byte == 0xF7 and self._status == 0xF0:
                self._pending.append(byte)
                self._complete(time_us)
            elif byte >= 0x80:
                self.reset()
                if byte == 0xF6:
                    self._data.append(byte)
                    self._times.append(time_us)
                elif byte <= 0xF3:
                    self._status = byte
                    self._pending.append(byte)
            elif self._status:
                if not self._pending:
                    self._pending.append(self._status)
                self._pending.append(byte)
                if self._status != 0xF0 and len(self._pending) == _size(self._status):
                    self._complete(time_us)

    def events(self, endpoint: str) -> Iterator[tuple[int, Event]]:
        offset = 0
        for time_us in self._times:
            status = self._data[offset]
            if status == 0xF0:
                end = self._data.index(0xF7, offset + 1) + 1
            else:
                end = offset + _size(status)
            yield (
                time_us,
                Event(
                    at=time_us / 1_000_000,
                    kind='midi',
                    target=endpoint,
                    data=list(self._data[offset:end]),
                ),
            )
            offset = end

    def _complete(self, time_us: int) -> None:
        self._data.extend(self._pending)
        self._times.append(time_us)
        self._pending.clear()
        # Channel voice status carries over to the next data-only message.
        if self._status >= 0xF0:
            self._status = 0


def _size(status: int) -> int:
    if status < 0xF0:
        return 2 if status >> 4 in (0xC, 0xD) else 3
    if status in (0xF1, 0xF3):
        return 2
    return 3 if status == 0xF2 else 1
