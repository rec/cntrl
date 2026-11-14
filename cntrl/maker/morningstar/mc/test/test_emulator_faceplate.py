"""The MC6 Pro artwork, live displays and switch locations share one canvas."""

import gc
import os
from pathlib import Path

import pytest

pytest.importorskip('PySide6')
pytest.importorskip('middy')
from PySide6 import QtCore, QtGui, QtWidgets

from cntrl.emulator.backend import Session
from cntrl.emulator.gui import ControllerCanvas
from cntrl.emulator.layout import Canvas, EmulatorLayout
from cntrl.patch_switcher.models import Target, TargetDisplay


class FaceplateSession(Session):
    def __init__(self) -> None:
        self.layout = EmulatorLayout.load(
            Path(__file__).parents[1] / 'emulator' / 'mc6-pro.toml'
        )
        self.controller = self.layout.hardware()
        self.target = Target.model_construct(
            displays=[
                TargetDisplay(id=n, label=n, width=160, height=128)
                for n in self.layout.displays
            ]
        )
        self.frames = {
            n: v * (160 * 128)
            for n, v in zip(
                self.layout.displays,
                [b'\xf8\x00', b'\x07\xe0', b'\x00\x1f'],
                strict=True,
            )
        }
        self.failed = False
        self.inputs: list[tuple[str, bool]] = []

    def button(self, control_id: str, pressed: bool) -> None:
        self.inputs.append((control_id, pressed))


def test_mc6_faceplate_keeps_display_and_switch_positions(
    tmp_path: Path, capfd: pytest.CaptureFixture[str]
) -> None:
    os.environ['QT_QPA_PLATFORM'] = 'offscreen'
    existing = QtWidgets.QApplication.instance()
    assert existing is None or isinstance(existing, QtWidgets.QApplication)
    application = existing or QtWidgets.QApplication([])
    session = FaceplateSession()
    canvas = ControllerCanvas(session, lambda message: None)
    canvas.resize(1200, 640)
    try:
        image = canvas.grab().toImage()
        assert image.save(str(tmp_path / 'mc6-pro.png'))
        for n, color in zip(
            session.layout.displays, ['#ff0000', '#00ff00', '#0000ff'], strict=True
        ):
            point = canvas.placement_rect(session.layout.displays[n]).center().toPoint()
            assert image.pixelColor(point).name() == color
        for n in session.layout.controls:
            point = canvas.placement_rect(session.layout.controls[n]).center()
            assert Canvas(session.layout, 1200, 640).control(point.x(), point.y()) == n
            QtWidgets.QApplication.sendEvent(
                canvas,
                QtGui.QMouseEvent(
                    QtCore.QEvent.Type.MouseButtonPress,
                    point,
                    point,
                    QtCore.Qt.MouseButton.RightButton,
                    QtCore.Qt.MouseButton.RightButton,
                    QtCore.Qt.KeyboardModifier.NoModifier,
                ),
            )
            assert session.inputs[-1] == (n, True)
            assert canvas.grab().toImage().pixelColor(
                point.toPoint()
            ) != image.pixelColor(point.toPoint())
        assert 'missing font family' not in capfd.readouterr().err
    finally:
        canvas.close()
        canvas.deleteLater()
        application.sendPostedEvents(None, QtCore.QEvent.Type.DeferredDelete)
        gc.collect()
        if existing is None:
            application.shutdown()
