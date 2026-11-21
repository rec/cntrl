"""Compact replay history for one MIDI 1.0 input stream."""

from array import array
from collections.abc import Iterator

from .replay import Event


class MidiHistory:
    def __init__(self) -> None:
        self._data = bytearray()
        self._times = array('Q')
        self._resets: list[int] = []
        self._framer = _Framer()

    def reset(self) -> None:
        if self._framer.status:
            self._resets.append(len(self._data))
            self._framer = _Framer()

    def record(self, time_us: int, data: bytes) -> None:
        start = len(self._data)
        self._data.extend(data)
        for offset, byte in enumerate(data, start):
            if self._framer.feed(byte, offset) is not None:
                self._times.append(time_us)

    def events(self, endpoint: str) -> Iterator[tuple[int, Event]]:
        framer = _Framer()
        resets = iter(self._resets)
        reset_at = next(resets, -1)
        timestamps = iter(self._times)
        for offset, byte in enumerate(self._data):
            if offset == reset_at:
                framer = _Framer()
                reset_at = next(resets, -1)
            if (message := framer.feed(byte, offset)) is None:
                continue
            start, status = message
            if status >= 0xF8:
                data = [status]
            else:
                data = [b for b in self._data[start : offset + 1] if b < 0xF8]
                if data[0] < 0x80:
                    data.insert(0, status)
            time_us = next(timestamps)
            yield (
                time_us,
                Event(
                    at=time_us / 1_000_000,
                    kind='midi',
                    target=endpoint,
                    data=data,
                ),
            )


class _Framer:
    def __init__(self) -> None:
        self.status = 0
        self.start = 0
        self.remaining = 0

    def feed(self, byte: int, offset: int) -> tuple[int, int] | None:
        if byte >= 0xF8:
            return offset, byte
        if byte == 0xF7 and self.status == 0xF0:
            self.status = 0
            return self.start, 0xF0
        if byte >= 0x80:
            self.status = byte if byte <= 0xF3 else 0
            self.start = offset
            self.remaining = _data_size(byte)
            return (offset, byte) if byte == 0xF6 else None
        if not self.status or self.status == 0xF0:
            return None
        if not self.remaining:
            self.start = offset
            self.remaining = _data_size(self.status)
        self.remaining -= 1
        if self.remaining:
            return None
        status = self.status
        if status >= 0xF0:
            self.status = 0
        return self.start, status


def _data_size(status: int) -> int:
    if status < 0xF0:
        return 1 if status >> 4 in (0xC, 0xD) else 2
    return 2 if status == 0xF2 else int(status in (0xF1, 0xF3))
