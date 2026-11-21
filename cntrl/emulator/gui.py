"""Desktop presentation of catalogue controls and native runtime framebuffers."""

import sys
from collections.abc import Callable
from pathlib import Path
from time import monotonic_ns
from types import TracebackType

import tomlkit
from middy import midi
from PySide6 import QtCore, QtGui, QtWidgets

from .. import models
from .backend import Session
from .host_midi import HostMidi
from .layout import Canvas, Placement
from .localization import dialog_messages, messages
from .replay import Scenario


def display_rectangle(
    rectangle: QtCore.QRectF, width: int, height: int, pixel_ratio: float
) -> QtCore.QRectF:
    """Map each pedal pixel to an integer block on the physical screen."""
    scale = max(
        1,
        int(
            min(
                rectangle.width() * pixel_ratio / width,
                rectangle.height() * pixel_ratio / height,
            )
        ),
    )
    physical_width, physical_height = width * scale, height * scale
    left = round(rectangle.center().x() * pixel_ratio - physical_width / 2)
    top = round(rectangle.center().y() * pixel_ratio - physical_height / 2)
    return QtCore.QRectF(
        left / pixel_ratio,
        top / pixel_ratio,
        physical_width / pixel_ratio,
        physical_height / pixel_ratio,
    )


class ControllerCanvas(QtWidgets.QWidget):
    def __init__(self, session: Session, report: Callable[[str], None]) -> None:
        super().__init__()
        self.session = session
        self.report = report
        self.pointer: set[str] = set()
        self.keyboard: set[str] = set()
        self.latched: set[str] = set()
        self.active: set[str] = set()
        self.widgets: dict[str, QtWidgets.QWidget] = {}
        self.faceplate = (
            QtGui.QPixmap(str(session.layout.faceplate))
            if session.layout.faceplate is not None
            else None
        )
        if self.faceplate is not None and self.faceplate.isNull():
            raise ValueError(
                f'cannot load emulator faceplate: {session.layout.faceplate}'
            )
        self.setMinimumSize(400, 260)
        self.setFocusPolicy(QtCore.Qt.FocusPolicy.StrongFocus)
        for c in session.controller.controls:
            if isinstance(c, models.AbsoluteControl):
                slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal, self)
                slider.setRange(0, 1000)
                slider.setToolTip(c.name)
                slider.valueChanged.connect(
                    lambda value, control=c: self.absolute(control, value)
                )
                self.widgets[c.id] = slider
            elif isinstance(c, models.RelativeControl):
                dial = QtWidgets.QSpinBox(self)
                dial.setRange(-1_000_000, 1_000_000)
                dial.setToolTip(c.name)
                dial.valueChanged.connect(
                    lambda value, identity=c.id: self.relative(identity, value)
                )
                self.widgets[c.id] = dial
        self.relative_positions: dict[str, int] = {}

    def placement_rect(self, placement: Placement) -> QtCore.QRectF:
        canvas = Canvas(self.session.layout, self.width(), self.height())
        return QtCore.QRectF(
            canvas.left + placement.x * canvas.scale,
            canvas.top + placement.y * canvas.scale,
            placement.width * canvas.scale,
            placement.height * canvas.scale,
        )

    def resizeEvent(self, event: QtGui.QResizeEvent) -> None:
        super().resizeEvent(event)
        for n, w in self.widgets.items():
            w.setGeometry(self.placement_rect(self.session.layout.controls[n]).toRect())

    def paintEvent(self, event: QtGui.QPaintEvent) -> None:
        painter = QtGui.QPainter(self)
        painter.fillRect(self.rect(), QtGui.QColor('#17191e'))
        painter.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        if self.faceplate is not None:
            painter.setRenderHint(QtGui.QPainter.RenderHint.SmoothPixmapTransform)
            painter.drawPixmap(
                self.placement_rect(
                    Placement(
                        x=0,
                        y=0,
                        width=self.session.layout.width,
                        height=self.session.layout.height,
                    )
                ),
                self.faceplate,
                QtCore.QRectF(self.faceplate.rect()),
            )
            painter.setRenderHint(
                QtGui.QPainter.RenderHint.SmoothPixmapTransform, False
            )
        for c in self.session.controller.controls:
            if not isinstance(c, models.Button):
                continue
            rectangle = self.placement_rect(self.session.layout.controls[c.id])
            if self.faceplate is not None:
                if c.id in self.active:
                    painter.setBrush(QtGui.QColor(115, 199, 160, 85))
                    painter.setPen(QtGui.QPen(QtGui.QColor('#73c7a0'), 3))
                    painter.drawEllipse(rectangle)
                continue
            painter.setBrush(
                QtGui.QColor('#73c7a0' if c.id in self.active else '#535a66')
            )
            painter.setPen(QtGui.QColor('#d8dde8'))
            painter.drawEllipse(rectangle)
            painter.drawText(rectangle, QtCore.Qt.AlignmentFlag.AlignCenter, c.name)
        for d in self.session.target.displays:
            rectangle = self.placement_rect(self.session.layout.displays[d.id])
            painter.fillRect(rectangle, QtGui.QColor('black'))
            if (pixels := self.session.frames.get(d.id)) and len(
                pixels
            ) == d.width * d.height * 2:
                # QImage RGB16 is native-endian; the protocol is RGB565 BE.
                native = bytearray(pixels)
                if sys.byteorder == 'little':
                    native[0::2], native[1::2] = pixels[1::2], pixels[0::2]
                image = QtGui.QImage(
                    bytes(native),
                    d.width,
                    d.height,
                    d.width * 2,
                    QtGui.QImage.Format.Format_RGB16,
                )
                destination = display_rectangle(
                    rectangle, d.width, d.height, self.devicePixelRatioF()
                )
                painter.save()
                painter.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, False)
                painter.setRenderHint(
                    QtGui.QPainter.RenderHint.SmoothPixmapTransform, False
                )
                painter.setClipRect(rectangle)
                painter.drawImage(destination, image)
                painter.restore()
        for p in self.session.layout.ports:
            rectangle = self.placement_rect(p)
            painter.setPen(QtGui.QColor('#b1bac9'))
            painter.setBrush(QtGui.QColor('#333944'))
            painter.drawRect(rectangle)
            painter.drawText(
                rectangle,
                QtCore.Qt.AlignmentFlag.AlignCenter,
                port_description(self.session.controller.ports[p.port]),
            )
        painter.end()

    def synchronize(self) -> None:
        active = self.pointer | self.keyboard | self.latched
        if not self.session.failed:
            for n in sorted(self.active ^ active):
                self.session.button(n, n in active)
        self.active = active
        self.update()

    def mousePressEvent(self, event: QtGui.QMouseEvent) -> None:
        self.setFocus()
        identity = Canvas(self.session.layout, self.width(), self.height()).control(
            event.position().x(), event.position().y()
        )
        buttons = {
            c.id
            for c in self.session.controller.controls
            if isinstance(c, models.Button)
        }
        if identity not in buttons:
            return
        if (
            event.button() == QtCore.Qt.MouseButton.RightButton
            or event.modifiers() & QtCore.Qt.KeyboardModifier.ShiftModifier
        ):
            self.latched.symmetric_difference_update({identity})
        elif event.button() == QtCore.Qt.MouseButton.LeftButton:
            self.pointer.add(identity)
        self.synchronize()

    def mouseReleaseEvent(self, event: QtGui.QMouseEvent) -> None:
        if event.button() == QtCore.Qt.MouseButton.LeftButton:
            self.pointer.clear()
            self.synchronize()

    def keyPressEvent(self, event: QtGui.QKeyEvent) -> None:
        if event.isAutoRepeat():
            return
        if self.session.editing_name:
            if not event.modifiers() & (
                QtCore.Qt.KeyboardModifier.ControlModifier
                | QtCore.Qt.KeyboardModifier.AltModifier
                | QtCore.Qt.KeyboardModifier.MetaModifier
            ):
                keys: dict[int, int] = {
                    QtCore.Qt.Key.Key_Return: 10,
                    QtCore.Qt.Key.Key_Enter: 10,
                    QtCore.Qt.Key.Key_Escape: 27,
                    QtCore.Qt.Key.Key_Backspace: 8,
                }
                if event.key() in keys:
                    self.session.key(keys[event.key()])
                else:
                    for c in event.text():
                        if 32 <= ord(c) <= 126:
                            self.session.key(ord(c))
            self.update()
            event.accept()
            return
        identity = QtGui.QKeySequence(event.key()).toString().lower()
        if any(
            c.id == identity and isinstance(c, models.Button)
            for c in self.session.controller.controls
        ):
            self.keyboard.add(identity)
            self.synchronize()
        else:
            super().keyPressEvent(event)

    def keyReleaseEvent(self, event: QtGui.QKeyEvent) -> None:
        if not event.isAutoRepeat():
            self.keyboard.discard(QtGui.QKeySequence(event.key()).toString().lower())
            self.synchronize()

    def release(self) -> None:
        self.pointer.clear()
        self.keyboard.clear()
        self.latched.clear()
        self.synchronize()

    def focusOutEvent(self, event: QtGui.QFocusEvent) -> None:
        self.release()
        super().focusOutEvent(event)

    def absolute(self, control: models.AbsoluteControl, value: int) -> None:
        position = control.value_range.minimum + value / 1000 * (
            control.value_range.maximum - control.value_range.minimum
        )
        try:
            self.session.absolute(control.id, position)
        except ValueError as error:
            self.report(str(error))

    def relative(self, identity: str, value: int) -> None:
        previous = self.relative_positions.get(identity, 0)
        self.relative_positions[identity] = value
        try:
            self.session.relative(identity, value - previous)
        except ValueError as error:
            self.report(str(error))


class AuxiliaryContact(QtWidgets.QPushButton):
    def __init__(self, label: str, changed: Callable[[bool], None]) -> None:
        super().__init__(label)
        self.changed = changed
        self.physical = False
        self.latched = False
        self.pressed.connect(lambda: self.contact(True))
        self.released.connect(lambda: self.contact(False))

    def contact(self, pressed: bool) -> None:
        self.physical = pressed
        self.changed(pressed or self.latched)

    def mousePressEvent(self, event: QtGui.QMouseEvent) -> None:
        if (
            event.button() == QtCore.Qt.MouseButton.RightButton
            or event.modifiers() & QtCore.Qt.KeyboardModifier.ShiftModifier
        ):
            self.latched = not self.latched
            self.setDown(self.physical or self.latched)
            self.changed(self.physical or self.latched)
        else:
            super().mousePressEvent(event)

    def mouseReleaseEvent(self, event: QtGui.QMouseEvent) -> None:
        if self.physical:
            super().mouseReleaseEvent(event)
        self.setDown(self.physical or self.latched)

    def keyPressEvent(self, event: QtGui.QKeyEvent) -> None:
        if not event.isAutoRepeat():
            super().keyPressEvent(event)

    def keyReleaseEvent(self, event: QtGui.QKeyEvent) -> None:
        if not event.isAutoRepeat():
            super().keyReleaseEvent(event)
        self.setDown(self.physical or self.latched)

    def reset(self) -> None:
        self.physical = self.latched = False
        self.setDown(False)
        self.changed(False)


class EmulatorWindow(QtWidgets.QMainWindow):
    def __init__(self, session: Session) -> None:
        super().__init__()
        self.session = session
        self.language = session.language
        self.setLocale(QtCore.QLocale(self.language))
        self.translations = messages(self.language)
        self.dialog_translator = DialogTranslator(self)
        QtWidgets.QApplication.installTranslator(self.dialog_translator)
        self.captions: list[tuple[Callable[[str], None], str, dict[str, object]]] = []
        self.log = QtWidgets.QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.document().setMaximumBlockCount(2000)
        self.canvas = ControllerCanvas(session, self.log.appendPlainText)
        self.auxiliary_contacts: dict[int, list[AuxiliaryContact]] = {}
        self.auxiliary_masks: dict[int, int] = {}
        central = QtWidgets.QWidget()
        column = QtWidgets.QVBoxLayout(central)
        column.addWidget(self.canvas, 1)
        column.addWidget(self.caption(QtWidgets.QLabel(), 'help'))
        time_controls = QtWidgets.QHBoxLayout()
        self.running = self.caption(QtWidgets.QCheckBox(), 'real_time')
        self.running.setChecked(True)
        self.running.toggled.connect(self.clock_mode)
        time_controls.addWidget(self.running)
        self.step = QtWidgets.QSpinBox()
        self.step.setRange(1, 60_000)
        self.step.setValue(10)
        self.step.setSuffix(' ms')
        time_controls.addWidget(self.step)
        advance = self.caption(QtWidgets.QPushButton(), 'advance')
        advance.clicked.connect(self.advance)
        time_controls.addWidget(advance)
        release = self.caption(QtWidgets.QPushButton(), 'release')
        release.clicked.connect(self.release_inputs)
        time_controls.addWidget(release)
        screenshot = self.caption(QtWidgets.QPushButton(), 'screenshot')
        screenshot.clicked.connect(self.screenshot)
        time_controls.addWidget(screenshot)
        replay = self.caption(QtWidgets.QPushButton(), 'replay')
        replay.clicked.connect(self.save_replay)
        time_controls.addWidget(replay)
        self.clock_label = QtWidgets.QLabel()
        time_controls.addWidget(self.clock_label)
        column.addLayout(time_controls)
        inspector = QtWidgets.QTabWidget()
        self.blocked: dict[str, QtWidgets.QCheckBox] = {}
        self.connected: dict[str, QtWidgets.QCheckBox] = {}
        for panel, key in [
            (self.log, 'midi_tab'),
            (self.endpoint_controls(), 'connections_tab'),
            (self.expression_controls(), 'expression_tab'),
        ]:
            index = inspector.addTab(panel, self.text(key))
            self.captions.append(
                (lambda text, i=index: inspector.setTabText(i, text), key, {})
            )
        inspector.setMaximumHeight(270)
        column.addWidget(inspector)
        self.setCentralWidget(central)
        self.resize(1000, 850)
        self.host: dict[str, HostMidi] = {}
        self.origin = monotonic_ns()
        self.origin_us = session.time_us
        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.tick)
        self.timer.start(10)
        self.relay_values: dict[str, int] = {}
        self.display_updates()

    def text(self, key: str, **fields: object) -> str:
        return self.translations[key].format(**fields)

    def caption[T: QtWidgets.QLabel | QtWidgets.QAbstractButton](
        self, widget: T, key: str, **fields: object
    ) -> T:
        widget.setText(self.text(key, **fields))
        if isinstance(widget, QtWidgets.QLabel):
            widget.setWordWrap(True)
        self.captions.append((widget.setText, key, fields))
        return widget

    def clock_mode(self, running: bool) -> None:
        self.origin = monotonic_ns()
        self.origin_us = self.session.time_us

    def advance(self) -> None:
        if self.session.failed:
            return
        self.running.setChecked(False)
        try:
            self.session.advance(self.session.time_us + self.step.value() * 1000)
        except (OSError, ValueError) as error:
            self.runtime_failed(error)
            return
        self.display_updates()

    def endpoint_controls(self) -> QtWidgets.QWidget:
        panel = QtWidgets.QWidget()
        rows = QtWidgets.QVBoxLayout(panel)
        for e in self.session.target.midi_endpoints:
            if not (e.input or e.output):
                continue
            row = QtWidgets.QHBoxLayout()
            row.addWidget(QtWidgets.QLabel(e.label))
            connected = self.caption(QtWidgets.QCheckBox(), 'connected')
            connected.setChecked(True)
            connected.toggled.connect(
                lambda state, endpoint=e.name: self.session.connection(endpoint, state)
            )
            self.connected[e.name] = connected
            row.addWidget(connected)
            if e.input:
                value = QtWidgets.QLineEdit('90 3c 64')
                row.addWidget(value)
                send = self.caption(QtWidgets.QPushButton(), 'inject')
                send.clicked.connect(
                    lambda checked=False, endpoint=e.name, field=value: self.inject(
                        endpoint, field.text()
                    )
                )
                row.addWidget(send)
            if e.output:
                blocked = self.caption(QtWidgets.QCheckBox(), 'block')
                self.blocked[e.name] = blocked
                blocked.toggled.connect(
                    lambda state, endpoint=e.name: self.session.block(endpoint, state)
                )
                row.addWidget(blocked)
            host = self.caption(QtWidgets.QPushButton(), 'host')
            host.clicked.connect(
                lambda checked=False, endpoint=e.name: self.connect_host(endpoint)
            )
            row.addWidget(host)
            rows.addLayout(row)
        rows.addWidget(self.caption(QtWidgets.QLabel(), 'ports'))
        for p in self.session.controller.ports:
            label = QtWidgets.QLabel()
            self.captions.append(
                (
                    lambda text, label=label, port=p: label.setText(
                        port_description(port, text)
                    ),
                    'not_emulated',
                    {},
                )
            )
            label.setText(port_description(p, self.text('not_emulated')))
            rows.addWidget(label)
        return panel

    def expression_controls(self) -> QtWidgets.QWidget:
        panel = QtWidgets.QWidget()
        rows = QtWidgets.QFormLayout(panel)
        rows.addRow(self.caption(QtWidgets.QLabel(), 'expression_help'))
        rows.addRow(self.caption(QtWidgets.QLabel(), 'aux_help'))
        for name, modes in self.session.target.trs_ports.items():
            port = next(
                (e.port for e in self.session.target.midi_endpoints if e.name == name),
                None,
            )
            if port is None:
                rows.addRow(name, self.caption(QtWidgets.QLabel(), 'no_binding'))
                continue
            if 'expression' in modes:
                slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
                slider.setRange(0, 1023)
                slider.valueChanged.connect(
                    lambda value, index=port: self.session.expression(index, value)
                )
                rows.addRow(name, slider)
            if 'aux' in modes:
                contacts = QtWidgets.QWidget()
                buttons = QtWidgets.QHBoxLayout(contacts)
                self.auxiliary_masks[port] = 0
                self.auxiliary_contacts[port] = []
                for index in range(2):
                    button = AuxiliaryContact(
                        '',
                        lambda pressed, slot=port, contact=index: self.auxiliary(
                            slot, contact, pressed
                        ),
                    )
                    self.caption(button, 'contact', number=index + 1)
                    self.auxiliary_contacts[port].append(button)
                    buttons.addWidget(button)
                rows.addRow(
                    self.caption(QtWidgets.QLabel(), 'aux_label', name=name), contacts
                )
        self.relay_labels: dict[str, QtWidgets.QLabel] = {}
        for n in self.session.target.relay_outputs:
            label = QtWidgets.QLabel(self.text('unknown_title'))
            self.relay_labels[n] = label
            rows.addRow(n, label)
        if not self.session.target.trs_ports and not self.relay_labels:
            rows.addRow(self.caption(QtWidgets.QLabel(), 'no_outputs'))
        return panel

    def auxiliary(self, port: int, contact: int, pressed: bool) -> None:
        previous = self.auxiliary_masks[port]
        mask = previous | (1 << contact) if pressed else previous & ~(1 << contact)
        if previous != mask:
            self.auxiliary_masks[port] = mask
            if not self.session.failed:
                self.session.auxiliary(port, mask)

    def release_inputs(self) -> None:
        self.canvas.release()
        for contacts in self.auxiliary_contacts.values():
            for button in contacts:
                button.reset()

    def changeEvent(self, event: QtCore.QEvent) -> None:
        if (
            event.type() == QtCore.QEvent.Type.ActivationChange
            and not self.isActiveWindow()
        ):
            self.release_inputs()
        super().changeEvent(event)

    def inject(self, endpoint: str, text: str) -> None:
        try:
            self.session.midi(endpoint, bytes.fromhex(text))
        except ValueError as error:
            self.log.appendPlainText(str(error))
        self.display_updates()

    def connect_host(self, endpoint: str) -> None:
        if endpoint in self.host:
            self.release_inputs()
            self.session.cleanup()
            self.display_updates()
            if connection := self.host.pop(endpoint, None):
                connection.close()
            self.log.appendPlainText(self.text('host_disconnected', endpoint=endpoint))
            return
        declaration = next(
            e for e in self.session.target.midi_endpoints if e.name == endpoint
        )
        connection = HostMidi(declaration.input, declaration.output)
        try:
            connection.refresh()
            if declaration.input:
                names = [f'{i}: {p.name}' for i, p in enumerate(connection.input_ports)]
                choice, accepted = self.host_choice('host_input', endpoint, names)
                if accepted:
                    connection.connect_input(names.index(choice))
            if declaration.output:
                names = [
                    f'{i}: {p.name}' for i, p in enumerate(connection.output_ports)
                ]
                choice, accepted = self.host_choice('host_output', endpoint, names)
                if accepted:
                    connection.connect_output(names.index(choice))
            if connection.input_connected or connection.output_connected:
                self.host[endpoint] = connection
                self.log.appendPlainText(self.text('host_connected', endpoint=endpoint))
            else:
                connection.close()
        except (midi.MidiError, ValueError, TypeError, OSError) as error:
            connection.close()
            self.log.appendPlainText(str(error))

    def tick(self) -> None:
        if self.session.failed:
            self.timer.stop()
            self.running.setChecked(False)
            return
        if self.running.isChecked():
            try:
                self.session.advance(
                    self.origin_us + (monotonic_ns() - self.origin) // 1000
                )
            except (OSError, ValueError) as error:
                self.runtime_failed(error)
                return
        if self.host:
            for n, c in list(self.host.items()):
                try:
                    for p in c.receive():
                        self.session.midi(n, p)
                except (midi.MidiError, ValueError, TypeError, OSError) as error:
                    self.host_failed(n, error)
        self.display_updates()

    def runtime_failed(self, error: OSError | ValueError) -> None:
        self.timer.stop()
        self.running.setChecked(False)
        self.running.setEnabled(False)
        self.release_inputs()
        self.canvas.setEnabled(False)
        self.log.appendPlainText(self.text('stopped', error=error))

    def display_updates(self) -> None:
        if self.language != self.session.language:
            self.language = self.session.language
            self.setLocale(QtCore.QLocale(self.language))
            self.translations = messages(self.language)
            for setter, key, fields in self.captions:
                setter(self.text(key, **fields))
        mode = self.text('mirror' if self.session.mode == 'pedal mirror' else 'preview')
        self.setWindowTitle(
            self.session.controller.name
            + f' [{mode}]'
            + (
                f' [{self.text("provisional")}]'
                if self.session.layout.provisional
                else ''
            )
        )
        updates = self.session.messages[:]
        del self.session.messages[: len(updates)]
        for e in updates:
            if e.kind == 'midi':
                self.log.appendPlainText(
                    f'{e.time_us / 1000:.3f} ms {e.endpoint}: {e.data.hex(" ")}'
                )
                if e.endpoint in self.host:
                    connection = self.host[e.endpoint]
                    try:
                        connection.send(e.data)
                    except (midi.MidiError, ValueError, TypeError, OSError) as error:
                        self.host_failed(e.endpoint, error)
            elif e.kind == 'relay' and e.endpoint in self.relay_labels:
                self.relay_values[e.endpoint] = e.value
            elif e.kind == 'keyboard':
                self.log.appendPlainText(self.text('keyboard', data=e.data.hex(' ')))
            elif e.kind == 'firmware':
                self.log.appendPlainText(self.text('stock_message'))
            else:
                self.log.appendPlainText(e.message)
        for name, label in self.relay_labels.items():
            label.setText(
                self.text(
                    ('on' if self.relay_values[name] else 'off')
                    if name in self.relay_values
                    else 'unknown_title'
                )
            )
        if self.session.stock_firmware:
            self.timer.stop()
            self.running.setChecked(False)
            self.running.setEnabled(False)
            self.canvas.setEnabled(False)
            self.clock_label.setText(self.text('stock'))
            return
        self.clock_label.setText(f'{self.session.time_us / 1_000_000:.3f} s')
        self.canvas.update()

    def host_failed(self, endpoint: str, error: BaseException) -> None:
        connection = self.host.pop(endpoint)
        self.connected[endpoint].setChecked(False)
        connection.close()
        self.session.cleanup()
        self.log.appendPlainText(
            self.text('host_failed', endpoint=endpoint, error=error)
        )

    def host_choice(
        self, title: str, endpoint: str, names: list[str]
    ) -> tuple[str, bool]:
        dialog = QtWidgets.QInputDialog(self)
        dialog.setWindowTitle(self.text(title))
        dialog.setLabelText(endpoint)
        dialog.setComboBoxItems(names)
        dialog.setComboBoxEditable(False)
        dialog.setOkButtonText(self.text('ok'))
        dialog.setCancelButtonText(self.text('cancel'))
        accepted = dialog.exec() == QtWidgets.QDialog.DialogCode.Accepted
        choice = dialog.textValue()
        dialog.deleteLater()
        return choice, accepted

    def save_path(self, title: str, filename: str, file_filter: str) -> str:
        dialog = QtWidgets.QFileDialog(self, self.text(title))
        dialog.setOption(QtWidgets.QFileDialog.Option.DontUseNativeDialog)
        dialog.setAcceptMode(QtWidgets.QFileDialog.AcceptMode.AcceptSave)
        dialog.selectFile(filename)
        dialog.setNameFilter(self.text(file_filter))
        for label, key in [
            (QtWidgets.QFileDialog.DialogLabel.LookIn, 'look_in'),
            (QtWidgets.QFileDialog.DialogLabel.FileName, 'filename'),
            (QtWidgets.QFileDialog.DialogLabel.FileType, 'filetype'),
            (QtWidgets.QFileDialog.DialogLabel.Accept, 'save'),
            (QtWidgets.QFileDialog.DialogLabel.Reject, 'cancel'),
        ]:
            dialog.setLabelText(label, self.text(key))
        path = dialog.selectedFiles()[0] if dialog.exec() else ''
        dialog.deleteLater()
        return path

    def screenshot(self) -> None:
        path = self.save_path('screenshot_title', 'controller.png', 'png')
        if path and not self.canvas.grab().save(str(Path(path))):
            self.log.appendPlainText(self.text('screenshot_error', path=path))

    def save_replay(self) -> None:
        path = self.save_path('replay_title', 'replay.toml', 'toml')
        if not path:
            return
        scenario = Scenario(
            seed=self.session.random_seed,
            until=self.session.time_us / 1_000_000,
            events=self.session.events,
        )
        try:
            Path(path).write_text(tomlkit.dumps(scenario.model_dump(mode='json')))
        except OSError as error:
            self.log.appendPlainText(str(error))

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self.timer.stop()
        try:
            self.release_inputs()
            if not self.session.failed:
                self.session.cleanup()
                self.display_updates()
                self.session.release_all()
        except (OSError, ValueError) as error:
            self.runtime_failed(error)
        for c in self.host.values():
            c.close()
        self.host.clear()
        self.session.close()
        QtWidgets.QApplication.removeTranslator(self.dialog_translator)
        event.accept()


class DialogTranslator(QtCore.QTranslator):
    """Translate Qt's file-dialog chrome without relying on OS language packs."""

    def __init__(self, window: EmulatorWindow) -> None:
        super().__init__(window)
        self.window = window
        self.keys = {v: n for n, v in messages('en').items() if '{' not in v}

    def isEmpty(self) -> bool:
        return False

    def translate(
        self,
        context: str,
        sourceText: str,
        disambiguation: str | None = None,
        n: int = -1,
    ) -> str:
        source = sourceText.replace('&', '')
        if context in (
            'QFileDialog',
            'QFileSystemModel',
            'QPlatformTheme',
            'QDialogButtonBox',
            'QLineEdit',
            'QWidgetTextControl',
            'QAbstractSpinBox',
        ):
            source = {
                'Go back': 'Back',
                'Go forward': 'Forward',
                'Go to the parent directory': 'Parent Directory',
                'Create New Folder': 'New Folder',
                'Create a New Folder': 'New Folder',
                'Change to list view mode': 'List View',
                'Change to detail view mode': 'Detail View',
                'Kind': 'Type',
                'List of places and bookmarks': 'Sidebar',
                'Folder': 'Directory',
                'Directories': 'Directory',
            }.get(source, source)
            if source in self.keys:
                return self.window.text(self.keys[source])
            return dialog_messages(self.window.language).get(source, '')
        return ''


def port_description(
    port: models.Port | models.UsbPort | str, not_emulated: str = 'not emulated'
) -> str:
    if isinstance(port, models.Port):
        modes = ', '.join(f'{m.protocol.kind}: {m.direction}' for m in port.modes)
        unsupported = any(m.protocol.kind in ('cv', 'ethernet') for m in port.modes)
        return f'{port.name} ({modes})' + (f' [{not_emulated}]' if unsupported else '')
    if isinstance(port, models.UsbPort):
        return f'{port.kind}' + (' host' if port.host else '')
    return str(port)


def run_window(session: Session) -> None:
    application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    previous_hook = sys.excepthook
    window: EmulatorWindow | None = None
    reporting = False

    def unhandled(
        kind: type[BaseException], error: BaseException, trace: TracebackType | None
    ) -> None:
        nonlocal reporting
        if reporting:
            sys.__excepthook__(kind, error, trace)
            return
        reporting = True
        if window is not None:
            window.timer.stop()
        try:
            previous_hook(kind, error, trace)
        finally:
            application.exit(1)

    sys.excepthook = unhandled
    try:
        window = EmulatorWindow(session)
        window.show()
        result = application.exec()
    finally:
        sys.excepthook = previous_hook
    if result:
        sys.exit(result)
