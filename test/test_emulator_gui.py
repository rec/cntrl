"""Input and pixel presentation checks without a desktop or MIDI device."""

import gc
import os
from collections.abc import Iterator
from pathlib import Path

import pytest

pytest.importorskip('PySide6')
pytest.importorskip('middy')
from PySide6 import QtCore, QtGui, QtWidgets

from cntrl.emulator.backend import OutputEvent, Session
from cntrl.emulator.gui import ControllerCanvas, EmulatorWindow
from cntrl.emulator.layout import EmulatorLayout, Placement
from cntrl.models import Button, Controller
from cntrl.patch_switcher.models import MidiEndpoint, Target, TargetDisplay


class CapturedSession(Session):
    def __init__(self) -> None:
        self.controller = Controller(
            name='Test controller',
            controls=[Button(id='a', name='A'), Button(id='b', name='B')],
        )
        self.layout = EmulatorLayout(
            catalogue=Path('unused.toml'),
            controller='test',
            width=100,
            height=100,
            controls={
                'a': Placement(x=10, y=50, width=20, height=20),
                'b': Placement(x=60, y=50, width=20, height=20),
            },
            displays={'screen': Placement(x=10, y=10, width=80, height=20)},
        )
        self.target = Target.model_construct(
            displays=[TargetDisplay(id='screen', label='Screen', width=8, height=4)],
            midi_endpoints=[],
            trs_ports={},
            relay_outputs=[],
        )
        self.frames = {'screen': b'\xf8\x00' * 32}
        self.inputs: list[tuple[str, bool]] = []
        self.expressions: list[tuple[int, int]] = []
        self.connections: list[tuple[str, bool]] = []
        self.auxiliary_inputs: list[tuple[int, int]] = []
        self.messages: list[OutputEvent] = []
        self.time_us = 0
        self.held: set[str] = set()
        self.closed = False
        self.failed = False

    def button(self, control_id: str, pressed: bool) -> None:
        self.inputs.append((control_id, pressed))
        if pressed:
            self.held.add(control_id)
        else:
            self.held.discard(control_id)

    def advance(self, time_us: int) -> None:
        self.time_us = time_us

    def expression(self, port: int, value: int) -> None:
        self.expressions.append((port, value))

    def connection(self, endpoint: str, connected: bool) -> None:
        self.connections.append((endpoint, connected))

    def auxiliary(self, port: int, contacts: int) -> None:
        self.auxiliary_inputs.append((port, contacts))

    def close(self) -> None:
        self.closed = True

    def cleanup(self) -> None:
        pass


class FailedSession(CapturedSession):
    def __init__(self) -> None:
        super().__init__()
        self.advance_calls = 0

    def advance(self, time_us: int) -> None:
        self.advance_calls += 1
        self.failed = True
        raise ValueError('backend timed out')

    def button(self, control_id: str, pressed: bool) -> None:
        assert not self.failed
        super().button(control_id, pressed)

    def cleanup(self) -> None:
        assert not self.failed


@pytest.fixture(scope='module')
def application() -> Iterator[QtWidgets.QApplication]:
    os.environ['QT_QPA_PLATFORM'] = 'offscreen'
    existing = QtWidgets.QApplication.instance()
    assert existing is None or isinstance(existing, QtWidgets.QApplication)
    app = existing or QtWidgets.QApplication([])
    yield app
    # Closing a widget only hides it. Destroy the test widgets before PySide's
    # interpreter-exit cleanup visits wrappers whose Python state is going away.
    for widget in app.topLevelWidgets():
        widget.close()
        widget.deleteLater()
    app.sendPostedEvents(None, QtCore.QEvent.Type.DeferredDelete)
    gc.collect()
    if existing is None:
        app.shutdown()
        assert QtWidgets.QApplication.instance() is None


def click(
    canvas: ControllerCanvas, identity: str, button: QtCore.Qt.MouseButton
) -> None:
    point = canvas.placement_rect(canvas.session.layout.controls[identity]).center()
    event = QtGui.QMouseEvent(
        QtCore.QEvent.Type.MouseButtonPress,
        point,
        point,
        button,
        button,
        QtCore.Qt.KeyboardModifier.NoModifier,
    )
    QtWidgets.QApplication.sendEvent(canvas, event)


def key(
    canvas: ControllerCanvas, event: QtCore.QEvent.Type, repeat: bool = False
) -> None:
    QtWidgets.QApplication.sendEvent(
        canvas,
        QtGui.QKeyEvent(
            event,
            QtCore.Qt.Key.Key_A,
            QtCore.Qt.KeyboardModifier.NoModifier,
            'a',
            repeat,
        ),
    )


def test_keyboard_repeat_does_not_retrigger_or_release_a_held_switch(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    canvas = ControllerCanvas(session, lambda message: None)
    key(canvas, QtCore.QEvent.Type.KeyPress)
    key(canvas, QtCore.QEvent.Type.KeyRelease, repeat=True)
    key(canvas, QtCore.QEvent.Type.KeyPress, repeat=True)
    assert session.inputs == [('a', True)]
    key(canvas, QtCore.QEvent.Type.KeyRelease)
    assert session.inputs == [('a', True), ('a', False)]
    canvas.close()


def test_latched_chord_and_focus_loss_release_each_switch(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    canvas = ControllerCanvas(session, lambda message: None)
    click(canvas, 'a', QtCore.Qt.MouseButton.RightButton)
    click(canvas, 'b', QtCore.Qt.MouseButton.LeftButton)
    assert session.inputs == [('a', True), ('b', True)]
    QtWidgets.QApplication.sendEvent(
        canvas, QtGui.QFocusEvent(QtCore.QEvent.Type.FocusOut)
    )
    assert session.inputs == [('a', True), ('b', True), ('a', False), ('b', False)]
    canvas.close()


def test_frame_pixels_keep_native_aspect_ratio_and_correct_rgb565_byte_order(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    canvas = ControllerCanvas(session, lambda message: None)
    canvas.resize(600, 400)
    image = canvas.grab().toImage()
    rectangle = canvas.placement_rect(session.layout.displays['screen'])
    center = rectangle.center().toPoint()
    assert image.pixelColor(center).getRgb() == (255, 0, 0, 255)
    assert image.pixelColor(int(rectangle.left()) + 2, center.y()).getRgb() == (
        0,
        0,
        0,
        255,
    )
    canvas.close()


def test_inspector_consumes_output_history_and_steps_the_same_clock(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    window = EmulatorWindow(session)
    window.timer.stop()
    session.messages.append(
        OutputEvent(kind='midi', endpoint='synth', data=b'\xf8', time_us=1000)
    )
    window.advance()
    assert session.time_us == 10000 and not window.running.isChecked()
    assert window.log.toPlainText() == '1.000 ms synth: f8'
    assert session.messages == []
    window.close()


def test_virtual_connection_toggle_reports_explicit_disconnect_and_reconnect(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    session.target = session.target.model_copy(
        update={'midi_endpoints': [MidiEndpoint(name='synth', label='Synth', route=0)]}
    )
    window = EmulatorWindow(session)
    window.timer.stop()
    assert session.connections == []
    window.connected['synth'].setChecked(False)
    window.connected['synth'].setChecked(True)
    assert session.connections == [('synth', False), ('synth', True)]
    window.close()
    assert session.closed


def test_expression_slider_uses_declared_sparse_port_binding(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    session.target = session.target.model_copy(
        update={
            'trs_ports': {'expression': ['expression']},
            'midi_endpoints': [
                MidiEndpoint(name='expression', label='Expression', route=0, port=3)
            ],
        }
    )
    window = EmulatorWindow(session)
    window.timer.stop()
    sliders = window.findChildren(QtWidgets.QSlider)
    assert len(sliders) == 1
    sliders[0].setValue(731)
    assert session.expressions == [(3, 731)]
    window.close()


def test_auxiliary_contacts_share_a_port_mask_and_release_on_focus_loss(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    session.target = session.target.model_copy(
        update={
            'trs_ports': {'auxiliary': ['aux']},
            'midi_endpoints': [
                MidiEndpoint(name='auxiliary', label='Auxiliary', route=0, port=2)
            ],
        }
    )
    window = EmulatorWindow(session)
    window.timer.stop()
    first, second = window.auxiliary_contacts[2]
    first.pressed.emit()
    second.pressed.emit()
    first.released.emit()
    assert session.auxiliary_inputs == [(2, 1), (2, 3), (2, 2)]
    QtWidgets.QApplication.sendEvent(
        window, QtCore.QEvent(QtCore.QEvent.Type.ActivationChange)
    )
    assert session.auxiliary_inputs[-1] == (2, 0)
    assert not any(window.auxiliary_masks.values())
    window.close()


@pytest.mark.parametrize('manual', [False, True])
def test_failed_runtime_stops_polling_and_closes_without_failed_requests(
    application: QtWidgets.QApplication, manual: bool
) -> None:
    session = FailedSession()
    window = EmulatorWindow(session)
    window.canvas.keyboard.add('a')
    window.canvas.synchronize()
    if manual:
        window.advance()
    else:
        window.tick()
    window.tick()
    window.advance()
    assert session.advance_calls == 1
    assert not window.timer.isActive() and not window.running.isChecked()
    assert window.log.toPlainText() == 'Native emulator stopped: backend timed out'
    assert window.canvas.active == set()
    window.close()
    assert session.closed
