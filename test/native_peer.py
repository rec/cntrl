"""Run the production C++ protocol peers through bounded binary pipes."""

import os
import subprocess
import time
from pathlib import Path
from queue import Empty, Queue
from tempfile import TemporaryFile
from threading import Thread, Timer
from types import TracebackType
from unittest.mock import patch

from scripts.native_tests import executable


class NativePeer:
    def __init__(self, command: list[str]) -> None:
        self.command = [str(executable(Path(command[0]))), *command[1:]]
        self.output: Queue[bytes] = Queue()
        self.pending = bytearray()
        self.stderr = TemporaryFile()

    def __enter__(self) -> str:
        self.process = subprocess.Popen(
            self.command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr,
        )
        self.reader = Thread(target=self.receive, daemon=True)
        self.reader.start()
        self.watchdog = Timer(180, self.process.kill)
        self.watchdog.start()
        self.backend = patch('cntrl.patch_switcher.transport.Serial', self.open)
        self.backend.start()
        return 'cntrl-test-peer'

    def open(
        self, port: str, *, baudrate: int, timeout: float, write_timeout: float
    ) -> 'PipePort':
        assert port == 'cntrl-test-peer' and baudrate == 115200
        return PipePort(self, timeout, write_timeout)

    def receive(self) -> None:
        assert self.process.stdout is not None
        while value := os.read(self.process.stdout.fileno(), 65536):
            self.output.put(value)
        self.output.put(b'')

    def __exit__(
        self,
        kind: type[BaseException] | None,
        value: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        self.backend.stop()
        self.watchdog.cancel()
        self.process.terminate()
        self.process.wait(timeout=5)
        self.reader.join(timeout=5)
        assert not self.reader.is_alive(), 'native peer did not close its output'
        assert self.process.stdin is not None and self.process.stdout is not None
        self.process.stdin.close()
        self.process.stdout.close()
        self.stderr.seek(0)
        errors = self.stderr.read().decode(errors='replace')
        self.stderr.close()
        assert not errors, errors


class PipePort:
    def __init__(self, peer: NativePeer, timeout: float, write_timeout: float) -> None:
        self.peer = peer
        self.timeout = timeout
        self.write_timeout = write_timeout

    def read(self, size: int) -> bytes:
        deadline = time.monotonic() + self.timeout
        while not self.peer.pending:
            try:
                value = self.peer.output.get(
                    timeout=max(0, deadline - time.monotonic())
                )
            except Empty:
                return b''
            if not value:
                raise OSError('native peer disconnected')
            self.peer.pending.extend(value)
        result = bytes(self.peer.pending[:size])
        del self.peer.pending[:size]
        return result

    def write(self, data: bytes) -> int:
        assert self.peer.process.stdin is not None
        count = self.peer.process.stdin.write(data)
        self.peer.process.stdin.flush()
        return count

    def reset_input_buffer(self) -> None:
        self.peer.pending.clear()
        while not self.peer.output.empty():
            self.peer.output.get_nowait()

    def close(self) -> None:
        # A host reconnect must retain the device process and session state.
        pass
