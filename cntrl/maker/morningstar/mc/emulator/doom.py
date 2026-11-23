"""Drive the MC6 Pro Freedoom host using its frame and pedal protocol."""

from pathlib import Path
from time import monotonic_ns

from PySide6 import QtCore

WIDTH, HEIGHT = 160, 128
FRAME_BYTES = WIDTH * HEIGHT * 2
PEDAL_BITS = {label: 1 << bit for bit, label in enumerate('CBADEF')}


class DoomHost(QtCore.QProcess):
    frame = QtCore.Signal(bytes)
    stopped = QtCore.Signal(str)

    def __init__(self, path: Path, parent: QtCore.QObject) -> None:
        super().__init__(parent)
        path = path.resolve()
        self.setProgram(str(path))
        self.setArguments([str(path.with_name('e1m1.img'))])
        self.requested_at: int | None = None
        self.readyReadStandardOutput.connect(self.present)
        self.errorOccurred.connect(self.report_error)
        self.finished.connect(self.report_exit)

    def advance(self, pedals: set[str]) -> None:
        if self.requested_at is not None:
            if monotonic_ns() - self.requested_at > 3_000_000_000:
                self.stopped.emit('Doom host timed out waiting for a frame.')
            return
        if self.state() != QtCore.QProcess.ProcessState.Running:
            return
        self.requested_at = monotonic_ns()
        if self.write(bytes([sum(PEDAL_BITS.get(p, 0) for p in pedals)])) != 1:
            self.stopped.emit('Could not send input to the Doom host.')

    def present(self) -> None:
        if self.bytesAvailable() >= FRAME_BYTES:
            self.requested_at = None
            self.frame.emit(bytes(self.read(FRAME_BYTES).data()))

    def report_error(self, error: QtCore.QProcess.ProcessError) -> None:
        if error != QtCore.QProcess.ProcessError.Crashed:
            self.stopped.emit(self.errorString())

    def report_exit(self, code: int) -> None:
        error = (
            bytes(self.readAllStandardError().data()).decode(errors='replace').strip()
        )
        self.stopped.emit(error or f'Doom host exited ({code}).')
