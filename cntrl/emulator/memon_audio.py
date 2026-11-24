"""Play the native Memon game's notes through the desktop audio output."""

from array import array
from math import sin, tau

from PySide6 import QtCore, QtMultimedia


class Tone:
    def __init__(self, note: int, rate: int) -> None:
        self.step = tau * 440 * 2 ** ((note - 69) / 12) / rate
        self.phase = 0.0
        self.gain = 0.0
        self.target = 0.0

    def mix(self, samples: list[float], envelope: float) -> None:
        gain, phase = self.gain, self.phase
        for i in range(len(samples)):
            gain += (self.target - gain) * envelope
            samples[i] += sin(phase) * gain
            phase += self.step
        self.gain, self.phase = gain, phase % tau


class MemonSynth:
    def __init__(self, rate: int) -> None:
        self.rate = rate
        self.envelope = 1 - 2 ** (-1000 / (3 * rate))
        self.tones: dict[int, Tone] = {}

    def note(self, data: bytes, legato: bool) -> None:
        status, pitch, velocity = data
        if status & 0xF0 == 0x90 and velocity:
            if legato:
                for tone in self.tones.values():
                    tone.target = 0.0
            if pitch not in self.tones:
                self.tones[pitch] = Tone(pitch, self.rate)
            self.tones[pitch].target = 0.16 * velocity / 127
        elif pitch in self.tones:
            self.tones[pitch].target = 0.0

    def render(self, frames: int, channels: int = 1) -> bytes:
        samples = [0.0] * frames
        for pitch, tone in list(self.tones.items()):
            tone.mix(samples, self.envelope)
            if not tone.target and tone.gain < 1 / 32768:
                del self.tones[pitch]
        return array(
            'h',
            (
                max(-32768, min(32767, round(s * 32767)))
                for s in samples
                for _ in range(channels)
            ),
        ).tobytes()


class MemonAudio(QtCore.QObject):
    unavailable = QtCore.Signal()

    def __init__(self, parent: QtCore.QObject) -> None:
        super().__init__(parent)
        self.sink: QtMultimedia.QAudioSink | None = None
        self.output: QtCore.QIODevice | None = None
        self.synth = MemonSynth(48000)
        self.channels = 1
        self.failed = False
        self.timer = QtCore.QTimer(self)
        self.timer.setTimerType(QtCore.Qt.TimerType.PreciseTimer)
        self.timer.setInterval(10)
        self.timer.timeout.connect(self.pump)

    def note(self, data: bytes, legato: bool) -> None:
        if self.failed:
            return
        if self.output is None:
            if data[0] & 0xF0 != 0x90 or not data[2]:
                return
            device = QtMultimedia.QMediaDevices.defaultAudioOutput()
            format = device.preferredFormat()
            format.setSampleFormat(QtMultimedia.QAudioFormat.SampleFormat.Int16)
            if device.isNull() or not device.isFormatSupported(format):
                self.fail()
                return
            self.channels = format.channelCount()
            self.synth = MemonSynth(format.sampleRate())
            self.sink = QtMultimedia.QAudioSink(device, format, self)
            self.sink.setBufferSize(format.bytesForDuration(40_000))
            self.output = self.sink.start()
            if self.output is None:
                self.fail()
                return
        self.synth.note(data, legato)
        if not self.timer.isActive():
            self.pump()
            if self.synth.tones:
                self.timer.start()

    def pump(self) -> None:
        sink, output = self.sink, self.output
        if sink is None or output is None:
            return
        if sink.state() == QtMultimedia.QtAudio.State.StoppedState:
            self.fail()
            return
        bytes_per_frame = 2 * self.channels
        frames = min(sink.bytesFree() // bytes_per_frame, self.synth.rate // 50)
        if frames and self.synth.tones:
            pcm = self.synth.render(frames, self.channels)
            if output.write(pcm) != len(pcm):
                self.fail()
                return
        if not self.synth.tones:
            self.timer.stop()

    def clear(self) -> None:
        self.timer.stop()
        self.synth.tones.clear()
        if self.sink is not None:
            self.sink.reset()
            self.sink.deleteLater()
            self.sink = None
            self.output = None

    def fail(self) -> None:
        self.clear()
        self.failed = True
        self.unavailable.emit()
