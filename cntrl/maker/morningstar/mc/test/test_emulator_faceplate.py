"""The MC6 Pro artwork, live displays and switch locations share one canvas."""

import gc
import os
from pathlib import Path

import pytest

pytest.importorskip('PySide6')
pytest.importorskip('middy')
from PySide6 import QtCore, QtGui, QtWidgets

from cntrl.emulator.backend import Session
from cntrl.emulator.gui import ControllerCanvas, display_rectangle
from cntrl.emulator.layout import Canvas, EmulatorLayout
from cntrl.patch_chooser.models import Target, TargetDisplay


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
        self.editing_name = False
        self.language = 'en'
        self.tooltip_context = ('', ())
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
    canvas = ControllerCanvas(session, lambda operation: operation())
    canvas.resize(1200, 640)
    try:
        image = canvas.grab().toImage()
        assert image.save(str(tmp_path / 'mc6-pro.png'))
        for n, color in zip(
            session.layout.displays, ['#ff0000', '#00ff00', '#0000ff'], strict=True
        ):
            point = canvas.placement_rect(session.layout.displays[n]).center().toPoint()
            assert image.pixelColor(point).name() == color
        # These are the canvas sizes from the Status clipping report.
        session.frames['lcd-1'] = (
            b'\xf8\x00' * 4 + b'\xff\xff' * 152 + b'\x00\x1f' * 4
        ) * 128
        canvas.frames = session.frames.copy()
        for height in (439, 420):
            canvas.resize(976, height)
            image = canvas.grab().toImage()
            bezel = canvas.placement_rect(session.layout.displays['lcd-1'])
            ratio = canvas.devicePixelRatioF()
            destination = display_rectangle(bezel, 160, 128, ratio)
            middle = round(destination.center().y() * ratio)
            assert bezel.contains(destination)
            assert (
                image.pixelColor(round(destination.left() * ratio), middle).name()
                == '#ff0000'
            )
            assert (
                image.pixelColor(round(destination.right() * ratio) - 1, middle).name()
                == '#0000ff'
            )
        canvas.resize(1200, 640)
        image = canvas.grab().toImage()
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
