"""Input and pixel presentation checks without a desktop or MIDI device."""

import gc
import os
import struct
import sys
from collections.abc import Callable, Iterator
from pathlib import Path
from threading import Event

import pytest

pytest.importorskip('PySide6')
pytest.importorskip('middy')
from PySide6 import QtCore, QtGui, QtWidgets

from cntrl import crashes
from cntrl.emulator import gui
from cntrl.emulator.backend import MAX_REPLAY_ADVANCE_US, OutputEvent, Session
from cntrl.emulator.gui import ControllerCanvas, EmulatorWindow, display_rectangle
from cntrl.emulator.host_midi import HostMidi
from cntrl.emulator.layout import EmulatorLayout, Placement
from cntrl.emulator.localization import dialog_messages, messages
from cntrl.localization import catalogue
from cntrl.models import Button, Controller
from cntrl.patch_chooser.models import MidiEndpoint, Target, TargetDisplay


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
        self.exit_code: int | None = None
        self.stock_firmware = False
        self.editing_name = False
        self.language = 'en'
        self.tooltip_context = ('', ())
        self.keys: list[int] = []
        self.mode = 'setup preview'

    def button(self, control_id: str, pressed: bool) -> None:
        self.inputs.append((control_id, pressed))
        if pressed:
            self.held.add(control_id)
        else:
            self.held.discard(control_id)

    def key(self, key: int) -> None:
        self.keys.append(key)

    def advance(self, time_us: int, *, replay: bool = False) -> None:
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

    def poll_exit(self) -> int | None:
        return self.exit_code


class FailedSession(CapturedSession):
    def __init__(self) -> None:
        super().__init__()
        self.advance_calls = 0

    def advance(self, time_us: int, *, replay: bool = False) -> None:
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
        if isinstance(widget, EmulatorWindow):
            wait_for(widget.close)
        else:
            widget.close()
        widget.deleteLater()
    app.sendPostedEvents(None, QtCore.QEvent.Type.DeferredDelete)
    gc.collect()
    if existing is None:
        app.shutdown()
        assert QtWidgets.QApplication.instance() is None


def wait_for(condition: Callable[[], bool]) -> None:
    loop = QtCore.QEventLoop()
    poll = QtCore.QTimer()

    def check() -> None:
        if condition():
            loop.quit()

    poll.timeout.connect(check)
    if not condition():
        poll.start(1)
        deadline = QtCore.QTimer()
        deadline.setSingleShot(True)
        deadline.timeout.connect(loop.quit)
        deadline.start(2000)
        loop.exec()
    assert condition()


def test_unhandled_callback_reports_once_and_exits(
    application: QtWidgets.QApplication, monkeypatch: pytest.MonkeyPatch
) -> None:
    reported: list[BaseException] = []

    def report(kind: type[BaseException], error: BaseException, trace: object) -> None:
        reported.append(error)

    def fail() -> None:
        raise RuntimeError('unexpected callback failure')

    monkeypatch.setattr(sys, 'excepthook', report)
    QtCore.QTimer.singleShot(0, fail)
    with pytest.raises(SystemExit) as stopped:
        gui.run_window(CapturedSession())
    assert stopped.value.code == 1
    assert len(reported) == 1
    assert str(reported[0]) == 'unexpected callback failure'
    assert sys.excepthook is report


@pytest.mark.parametrize('approve', [False, True])
def test_crash_dialog_shows_report_before_opening_browser(
    application: QtWidgets.QApplication,
    monkeypatch: pytest.MonkeyPatch,
    approve: bool,
) -> None:
    previews: list[tuple[str, bool]] = []
    urls: list[str] = []
    monkeypatch.setattr(crashes, 'build_ref', lambda: 'build-ref')

    def open_browser(url: str) -> bool:
        urls.append(url)
        return True

    def choose() -> None:
        dialog = application.activeModalWidget()
        if not isinstance(dialog, QtWidgets.QDialog):
            return
        preview = dialog.findChild(QtWidgets.QPlainTextEdit)
        if preview is not None:
            previews.append((preview.toPlainText(), bool(urls)))
        button = next(
            (
                b
                for b in dialog.findChildren(QtWidgets.QPushButton)
                if b.text() == 'Report Issue'
            ),
            None,
        )
        if approve and button is not None:
            button.click()
        else:
            dialog.reject()

    monkeypatch.setattr(crashes.webbrowser, 'open', open_browser)
    QtCore.QTimer.singleShot(0, choose)
    crashes.report('emulator', RuntimeError('previewed locally'), None)
    ((preview, browser_already_open),) = previews
    assert 'RuntimeError: previewed locally' in preview
    assert not browser_already_open
    assert len(urls) == int(approve)


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
    canvas = ControllerCanvas(session, lambda operation: operation())
    key(canvas, QtCore.QEvent.Type.KeyPress)
    key(canvas, QtCore.QEvent.Type.KeyRelease, repeat=True)
    key(canvas, QtCore.QEvent.Type.KeyPress, repeat=True)
    assert session.inputs == [('a', True)]
    key(canvas, QtCore.QEvent.Type.KeyRelease)
    assert session.inputs == [('a', True), ('a', False)]
    canvas.close()


def test_keyboard_chord_tracks_keys_without_character_text(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    session.controller = Controller(
        name='Test controller',
        controls=[Button(id='d', name='D'), Button(id='f', name='F')],
    )
    canvas = ControllerCanvas(session, lambda operation: operation())
    for event, code, text in [
        (QtCore.QEvent.Type.KeyPress, QtCore.Qt.Key.Key_D, ''),
        (QtCore.QEvent.Type.KeyPress, QtCore.Qt.Key.Key_F, 'f'),
        (QtCore.QEvent.Type.KeyRelease, QtCore.Qt.Key.Key_F, ''),
    ]:
        QtWidgets.QApplication.sendEvent(
            canvas,
            QtGui.QKeyEvent(event, code, QtCore.Qt.KeyboardModifier.NoModifier, text),
        )
    assert canvas.active == {'d'}
    assert session.inputs == [('d', True), ('f', True), ('f', False)]
    QtWidgets.QApplication.sendEvent(
        canvas,
        QtGui.QKeyEvent(
            QtCore.QEvent.Type.KeyRelease,
            QtCore.Qt.Key.Key_D,
            QtCore.Qt.KeyboardModifier.NoModifier,
            'D',
        ),
    )
    assert not canvas.active
    assert session.inputs[-1] == ('d', False)
    canvas.close()


def test_name_typing_takes_precedence_over_pedal_shortcuts(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    session.editing_name = True
    canvas = ControllerCanvas(session, lambda operation: operation())
    for code, text, modifier, repeat in [
        (QtCore.Qt.Key.Key_A, 'a', QtCore.Qt.KeyboardModifier.NoModifier, False),
        (QtCore.Qt.Key.Key_B, 'B', QtCore.Qt.KeyboardModifier.ShiftModifier, False),
        (QtCore.Qt.Key.Key_Backspace, '', QtCore.Qt.KeyboardModifier.NoModifier, False),
        (QtCore.Qt.Key.Key_Return, '', QtCore.Qt.KeyboardModifier.NoModifier, False),
        (QtCore.Qt.Key.Key_Enter, '', QtCore.Qt.KeyboardModifier.KeypadModifier, False),
        (QtCore.Qt.Key.Key_Escape, '', QtCore.Qt.KeyboardModifier.NoModifier, False),
        (QtCore.Qt.Key.Key_A, 'a', QtCore.Qt.KeyboardModifier.NoModifier, True),
        (QtCore.Qt.Key.Key_A, 'a', QtCore.Qt.KeyboardModifier.ControlModifier, False),
        (QtCore.Qt.Key.Key_A, 'a', QtCore.Qt.KeyboardModifier.AltModifier, False),
        (QtCore.Qt.Key.Key_A, 'a', QtCore.Qt.KeyboardModifier.MetaModifier, False),
        (QtCore.Qt.Key.Key_Eacute, 'é', QtCore.Qt.KeyboardModifier.NoModifier, False),
    ]:
        QtWidgets.QApplication.sendEvent(
            canvas,
            QtGui.QKeyEvent(QtCore.QEvent.Type.KeyPress, code, modifier, text, repeat),
        )
    key(canvas, QtCore.QEvent.Type.KeyRelease)
    assert session.keys == [ord('a'), ord('B'), 8, 10, 10, 27]
    assert not session.inputs and not canvas.active
    canvas.editing_name = False
    key(canvas, QtCore.QEvent.Type.KeyPress)
    key(canvas, QtCore.QEvent.Type.KeyRelease)
    assert session.inputs == [('a', True), ('a', False)]
    canvas.close()


def test_latched_chord_and_focus_loss_release_each_switch(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    canvas = ControllerCanvas(session, lambda operation: operation())
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
    canvas = ControllerCanvas(session, lambda operation: operation())
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


@pytest.mark.parametrize('ratio', [1.0, 1.25, 1.5, 2.0, 3.0])
def test_display_size_and_origin_align_to_physical_pixels(ratio: float) -> None:
    bezel = QtCore.QRectF(27.3, 15.7, 187.8, 150.2)
    destination = display_rectangle(bezel, 160, 128, ratio)
    scale = destination.width() * ratio / 160
    assert scale == pytest.approx(round(scale))
    assert scale >= 1
    assert destination.height() * ratio / 128 == pytest.approx(scale)
    assert destination.left() * ratio == pytest.approx(
        round(destination.left() * ratio)
    )
    assert destination.top() * ratio == pytest.approx(round(destination.top() * ratio))
    assert abs(destination.center().x() - bezel.center().x()) * ratio <= 0.5 + 1e-9
    assert abs(destination.center().y() - bezel.center().y()) * ratio <= 0.5 + 1e-9


def test_display_pixel_columns_have_uniform_width_at_fractional_window_sizes(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    session.frames['screen'] = b'\xf8\x00\x07\xe0' * 16
    canvas = ControllerCanvas(session, lambda operation: operation())
    canvas.resize(603, 407)
    image = canvas.grab().toImage()
    ratio = canvas.devicePixelRatioF()
    destination = display_rectangle(
        canvas.placement_rect(session.layout.displays['screen']), 8, 4, ratio
    )
    left = round(destination.left() * ratio)
    middle = round(destination.center().y() * ratio)
    scale = round(destination.width() * ratio / 8)
    for column in range(8):
        for offset in range(scale):
            assert image.pixelColor(left + column * scale + offset, middle).name() == (
                '#ff0000' if column % 2 == 0 else '#00ff00'
            )
    canvas.close()


def test_faceplate_keeps_live_pixels_and_switch_feedback_aligned_when_resized(
    application: QtWidgets.QApplication, tmp_path: Path
) -> None:
    artwork = tmp_path / 'faceplate.svg'
    artwork.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">'
        '<rect width="100" height="100" fill="#243648"/></svg>'
    )
    session = CapturedSession()
    session.layout = session.layout.model_copy(update={'faceplate': artwork})
    canvas = ControllerCanvas(session, lambda operation: operation())
    for width, height in [(600, 400), (400, 600)]:
        canvas.resize(width, height)
        image = canvas.grab().toImage()
        switch = canvas.placement_rect(session.layout.controls['a']).center().toPoint()
        screen = (
            canvas.placement_rect(session.layout.displays['screen']).center().toPoint()
        )
        assert image.pixelColor(switch).name() == '#243648'
        assert image.pixelColor(screen).name() == '#ff0000'
        click(canvas, 'a', QtCore.Qt.MouseButton.RightButton)
        assert canvas.grab().toImage().pixelColor(switch).name() != '#243648'
        click(canvas, 'a', QtCore.Qt.MouseButton.RightButton)
    assert session.inputs == [('a', True), ('a', False)] * 2
    canvas.close()


def test_unreadable_faceplate_reports_its_path(
    application: QtWidgets.QApplication, tmp_path: Path
) -> None:
    session = CapturedSession()
    path = tmp_path / 'missing.svg'
    session.layout = session.layout.model_copy(update={'faceplate': path})
    with pytest.raises(ValueError, match='cannot load emulator faceplate'):
        ControllerCanvas(session, lambda operation: operation())


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
    wait_for(lambda: window.clock_label.text() == '0.010 s')
    assert session.time_us == 10000 and not window.running.isChecked()
    assert window.log.toPlainText() == '1.000 ms synth: f8'
    assert session.messages == []
    wait_for(window.close)


def test_running_clock_splits_a_long_gap_and_reaches_the_elapsed_time(
    application: QtWidgets.QApplication, monkeypatch: pytest.MonkeyPatch
) -> None:
    session = CapturedSession()
    session.time_us = 1234
    requests: list[bytes] = []
    monkeypatch.setattr(session, 'advance', Session.advance.__get__(session))
    monkeypatch.setattr(session, 'request', requests.append)
    window = EmulatorWindow(session)
    window.timer.stop()
    window.running.setChecked(True)
    elapsed_us = 2 * MAX_REPLAY_ADVANCE_US + 2153
    monkeypatch.setattr(gui, 'monotonic_ns', lambda: window.origin + elapsed_us * 1000)

    window.tick()
    wait_for(lambda: window.time_us == 1234 + elapsed_us)

    assert session.time_us == 1234 + elapsed_us
    assert requests == [
        struct.pack('<BQB', 1, 1234 + MAX_REPLAY_ADVANCE_US, 0),
        struct.pack('<BQB', 1, 1234 + 2 * MAX_REPLAY_ADVANCE_US, 0),
        struct.pack('<BQ', 1, 1234 + elapsed_us),
    ]
    wait_for(window.close)


def test_stalled_clock_keeps_qt_responsive_and_serializes_pedal_input(
    application: QtWidgets.QApplication,
) -> None:
    entered = Event()
    acknowledge = Event()

    class StalledSession(CapturedSession):
        def advance(self, time_us: int, *, replay: bool = False) -> None:
            entered.set()
            if not acknowledge.wait(3):
                raise TimeoutError('test did not release stalled request')
            super().advance(time_us, replay=replay)

        def button(self, control_id: str, pressed: bool) -> None:
            assert acknowledge.is_set()
            super().button(control_id, pressed)

    session = StalledSession()
    window = EmulatorWindow(session)
    window.timer.stop()
    try:
        window.tick()
        wait_for(entered.is_set)
        rendered: list[QtGui.QPixmap] = []

        def press_switch() -> None:
            click(window.canvas, 'a', QtCore.Qt.MouseButton.RightButton)
            rendered.append(window.canvas.grab())

        QtCore.QTimer.singleShot(0, press_switch)
        wait_for(lambda: bool(rendered))
        assert not rendered[0].isNull()
        assert window.canvas.active == {'a'}
        assert session.inputs == []
        assert window.clock_label.text() == '0.000 s'
    finally:
        acknowledge.set()
        wait_for(window.close)
    assert session.inputs == [('a', True), ('a', False)]


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
    wait_for(lambda: len(session.connections) == 2)
    assert session.connections == [('synth', False), ('synth', True)]
    wait_for(window.close)
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
    wait_for(lambda: bool(session.expressions))
    assert session.expressions == [(3, 731)]
    wait_for(window.close)


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
    wait_for(lambda: len(session.auxiliary_inputs) == 3)
    assert session.auxiliary_inputs == [(2, 1), (2, 3), (2, 2)]
    QtWidgets.QApplication.sendEvent(
        window, QtCore.QEvent(QtCore.QEvent.Type.ActivationChange)
    )
    wait_for(lambda: len(session.auxiliary_inputs) == 4)
    assert session.auxiliary_inputs[-1] == (2, 0)
    assert not any(window.auxiliary_masks.values())
    wait_for(window.close)


@pytest.mark.parametrize('mode', ['manual', 'realtime', 'paused'])
def test_failed_runtime_stops_polling_and_closes_without_failed_requests(
    application: QtWidgets.QApplication, mode: str
) -> None:
    class Output:
        def __init__(self) -> None:
            self.packets: list[bytes] = []
            self.closed = False

        def send(self, packet: bytes) -> None:
            assert not self.closed
            self.packets.append(packet)

        def close(self) -> None:
            self.closed = True

    session = FailedSession()
    window = EmulatorWindow(session)
    window.timer.setInterval(60_000)
    output = Output()
    connection = HostMidi(False, True)
    connection.output = output
    connection.output_connected = True
    window.host['synth'] = connection
    # Two strikes of the held note, one already released, and a held sustain.
    # The other channel's note and pedal were both released normally.
    played = [
        b'\x90\x3c\x64',
        b'\x90\x3c\x64',
        b'\x90\x3c\x00',
        b'\xb0\x40\x7f',
        b'\x91\x40\x64',
        b'\x81\x40\x00',
        b'\xb1\x40\x7f',
        b'\xb1\x40\x00',
    ]
    session.messages.extend(
        OutputEvent(kind='midi', endpoint='synth', data=p, time_us=0) for p in played
    )
    window.display_updates()
    assert output.packets == played
    window.log.clear()
    window.canvas.keyboard.add('a')
    window.canvas.synchronize()
    wait_for(lambda: window.request is None)
    if mode == 'manual':
        window.advance()
    else:
        if mode == 'paused':
            window.running.setChecked(False)
            session.exit_code = 1
        window.tick()
    wait_for(lambda: not window.running.isEnabled())
    window.tick()
    window.advance()
    assert session.advance_calls == (0 if mode == 'paused' else 1)
    assert not window.timer.isActive() and not window.running.isChecked()
    reason = (
        'native emulator disconnected (exit 1)'
        if mode == 'paused'
        else 'backend timed out'
    )
    assert window.log.toPlainText() == f'Native emulator stopped: {reason}'
    assert window.canvas.active == set()
    assert output.closed
    assert output.packets == played + [b'\x80\x3c\x00', b'\xb0\x40\x00']
    wait_for(window.close)
    assert session.closed


def test_simulated_firmware_transition_is_visible_and_stops_controls(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    window = EmulatorWindow(session)
    session.stock_firmware = True
    session.messages.append(
        OutputEvent(kind='firmware', time_us=0, message='Simulated stock firmware')
    )
    window.display_updates()
    assert window.log.toPlainText() == (
        'Simulated stock firmware selected; restart the session to return to CNTRL.'
    )
    assert window.clock_label.text() == 'Simulated stock firmware'
    assert not window.timer.isActive()
    assert not window.running.isChecked() and not window.running.isEnabled()
    assert not window.canvas.isEnabled()
    wait_for(window.close)


@pytest.mark.parametrize(
    'language,caption',
    [
        ('en', 'Real time'),
        ('fr', 'Temps réel'),
        ('de', 'Echtzeit'),
        ('es', 'Tiempo real'),
        ('it', 'Tempo reale'),
        ('pt-PT', 'Tempo real'),
        ('nl', 'Realtime'),
        ('pl', 'Czas rzeczywisty'),
        ('cs', 'Reálný čas'),
        ('sk', 'Reálny čas'),
        ('hu', 'Valós idő'),
        ('ro', 'Timp real'),
        ('sv', 'Realtid'),
        ('da', 'Realtid'),
        ('nb', 'Sanntid'),
        ('fi', 'Reaaliaika'),
        ('tr', 'Gerçek zaman'),
    ],
)
def test_desktop_follows_pedal_language_without_resetting_inputs(
    application: QtWidgets.QApplication, language: str, caption: str
) -> None:
    session = CapturedSession()
    session.target = session.target.model_copy(
        update={
            'trs_ports': {'expression': ['expression']},
            'midi_endpoints': [
                MidiEndpoint(name='expression', label='Expression', route=0, port=3)
            ],
            'relay_outputs': ['relay'],
        }
    )
    window = EmulatorWindow(session)
    window.timer.stop()
    slider = window.findChild(QtWidgets.QSlider)
    assert slider is not None
    slider.setValue(731)
    window.step.setValue(27)
    click(window.canvas, 'a', QtCore.Qt.MouseButton.RightButton)
    wait_for(lambda: window.request is None)
    session.messages.append(
        OutputEvent(kind='relay', endpoint='relay', value=1, time_us=0)
    )
    session.language = language
    window.display_updates()
    assert window.running.text() == caption
    assert (
        window.relay_labels['relay'].text()
        == catalogue.translation(language).messages['on'].text
    )
    assert window.connected['expression'].isChecked()
    assert slider.value() == 731 and session.expressions == [(3, 731)]
    assert window.step.value() == 27 and window.canvas.active == {'a'}
    assert session.inputs == [('a', True)]
    assert session.controller.controls[0].name == 'A'
    session.language = 'en'
    window.display_updates()
    assert window.running.text() == 'Real time'
    assert window.relay_labels['relay'].text() == 'On'
    wait_for(window.close)


def test_desktop_starts_in_the_mirrored_pedals_language(
    application: QtWidgets.QApplication,
) -> None:
    session = CapturedSession()
    session.mode = 'pedal mirror'
    session.language = 'fr'
    window = EmulatorWindow(session)
    window.timer.stop()
    assert window.running.text() == 'Temps réel'
    assert window.windowTitle() == 'Test controller [miroir de la pédale]'
    assert set(catalogue.locales()) == {
        'en',
        'fr',
        'de',
        'es',
        'it',
        'pt-PT',
        'nl',
        'pl',
        'cs',
        'sk',
        'hu',
        'ro',
        'sv',
        'da',
        'nb',
        'fi',
        'tr',
    }
    for language in catalogue.locales():
        assert messages(language).keys() == messages('en').keys()
        assert dialog_messages(language).keys() == dialog_messages('en').keys()
    wait_for(window.close)


def test_hover_uses_screen_page_button_focus_and_active_language(
    application: QtWidgets.QApplication, monkeypatch: pytest.MonkeyPatch
) -> None:
    session = CapturedSession()
    session.tooltip_context = ('status', (('patch-chooser', 1),))
    canvas = ControllerCanvas(session, lambda action: action())
    shown: list[str] = []
    monkeypatch.setattr(
        QtWidgets.QToolTip, 'showText', lambda position, text, *args: shown.append(text)
    )
    screen = canvas.placement_rect(session.layout.displays['screen']).center().toPoint()
    button = canvas.placement_rect(session.layout.controls['a']).center().toPoint()

    def hover(point: QtCore.QPoint) -> None:
        event = QtGui.QHelpEvent(
            QtCore.QEvent.Type.ToolTip, point, canvas.mapToGlobal(point)
        )
        application.sendEvent(canvas, event)

    hover(screen)
    hover(button)
    assert shown == []
    monkeypatch.setattr(
        gui,
        'tooltip_messages',
        lambda language: {
            'patch-chooser': {'window-1': f'{language} screen'},
            'status': {'button-a': f'{language} button'},
            'time': {'button-a': f'{language} time'},
        },
    )
    hover(screen)
    hover(button)
    session.language = 'fr'
    session.tooltip_context = ('time', (('patch-chooser', 1),))
    canvas.update_tooltip_context()
    hover(screen)
    hover(button)
    assert shown == [
        '<p>en screen</p>',
        '<p>en button</p>',
        '<p>fr screen</p>',
        '<p>fr time</p>',
    ]


def test_replay_controls_require_recording_before_save(
    application: QtWidgets.QApplication,
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    session = CapturedSession()
    actions: list[bool | Path] = []
    monkeypatch.setattr(session, 'record_replay', actions.append)
    monkeypatch.setattr(session, 'write_replay', actions.append)
    window = EmulatorWindow(session)
    window.timer.stop()
    path = tmp_path / 'replay.toml'
    monkeypatch.setattr(window, 'save_path', lambda *_: str(path))
    assert not window.recording.isChecked()
    assert not window.replay.isEnabled()
    window.recording.click()
    wait_for(window.replay.isEnabled)
    window.recording.click()
    window.replay.click()
    wait_for(lambda: window.request is None)
    assert actions == [True, False, path]
    window.close()
    wait_for(lambda: window.session_closed)


def test_save_and_host_dialogs_use_the_pedals_language(
    application: QtWidgets.QApplication,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    session = CapturedSession()
    session.language = 'fr'
    window = EmulatorWindow(session)
    window.timer.stop()

    def save_dialog(dialog: QtWidgets.QFileDialog) -> int:
        assert dialog.windowTitle() == 'Enregistrer une capture de l’émulateur'
        assert (
            dialog.labelText(QtWidgets.QFileDialog.DialogLabel.Accept) == 'Enregistrer'
        )
        assert (
            dialog.labelText(QtWidgets.QFileDialog.DialogLabel.FileName)
            == 'Nom du fichier :'
        )
        assert dialog.testOption(QtWidgets.QFileDialog.Option.DontUseNativeDialog)
        assert QtCore.QCoreApplication.translate('QFileSystemModel', 'Name') == 'Nom'
        assert (
            QtCore.QCoreApplication.translate('QFileDialog', 'Go back') == 'Précédent'
        )
        return 0

    def host_dialog(dialog: QtWidgets.QInputDialog) -> int:
        assert dialog.windowTitle() == 'Entrée MIDI de l’hôte'
        assert dialog.okButtonText() == 'OK'
        assert dialog.cancelButtonText() == 'Annuler'
        return 0

    monkeypatch.setattr(QtWidgets.QFileDialog, 'exec', save_dialog)
    monkeypatch.setattr(QtWidgets.QInputDialog, 'exec', host_dialog)
    assert window.save_path('screenshot_title', 'controller.png', 'png') == ''
    assert not window.host_choice('host_input', 'DIN', ['Port 1'])[1]
    wait_for(window.close)
