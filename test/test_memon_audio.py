"""Regression for Memon's mono and chord playback at 48 kHz."""

import wave
from pathlib import Path

import pytest
from pytest_regressions.file_regression import FileRegressionFixture

pytest.importorskip('PySide6.QtMultimedia')

from cntrl.emulator.memon_audio import MemonSynth  # noqa: E402


def test_memon_note_lifetimes(
    tmp_path: Path, file_regression: FileRegressionFixture
) -> None:
    synth = MemonSynth(48000)
    with wave.open(str(tmp_path / 'memon.wav'), 'wb') as audio:
        audio.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
        synth.note(bytes((0x90, 60, 100)), legato=True)
        audio.writeframes(synth.render(12000))
        synth.note(bytes((0x90, 64, 100)), legato=True)
        audio.writeframes(synth.render(12000))
        assert list(synth.tones) == [64]
        synth.note(bytes((0x80, 64, 0)), legato=True)
        synth.note(bytes((0x90, 60, 100)), legato=False)
        synth.note(bytes((0x90, 67, 100)), legato=False)
        audio.writeframes(synth.render(12000))
        assert set(synth.tones) == {60, 67}
        synth.note(bytes((0x80, 60, 0)), legato=False)
        synth.note(bytes((0x90, 67, 0)), legato=False)
        audio.writeframes(synth.render(12000))
        assert not synth.tones
    file_regression.check(
        (tmp_path / 'memon.wav').read_bytes(), binary=True, extension='.wav'
    )
