"""Desktop presentation of catalogue controls and native runtime framebuffers."""

import sys
from collections import deque
from collections.abc import Callable
from concurrent.futures import Future, ThreadPoolExecutor
from functools import partial
from html import escape
from math import ceil, floor
from pathlib import Path
from time import monotonic_ns
from types import TracebackType

from PySide6 import QtCore, QtGui, QtWidgets

from .. import models
from ..maker.morningstar.mc.emulator import doom, doom_cards
from .backend import Session
from .corrections import Correction, CorrectionBatch, TranslationRegion, issue_body
from .host_midi import MIDI_ERRORS, HostMidi
from .layout import Canvas, Placement
from .localization import dialog_messages, messages, tooltip_messages


def display_rectangle(
    rectangle: QtCore.QRectF, width: int, height: int, pixel_ratio: float
) -> QtCore.QRectF:
    """Use whole physical pixels for enlargement and fit smaller bezels."""
    left_edge = ceil(rectangle.left() * pixel_ratio)
    top_edge = ceil(rectangle.top() * pixel_ratio)
    right_edge = floor(rectangle.right() * pixel_ratio)
    bottom_edge = floor(rectangle.bottom() * pixel_ratio)
    fit = min((right_edge - left_edge) / width, (bottom_edge - top_edge) / height)
    if fit <= 0:
        return QtCore.QRectF()
    scale = int(fit) if fit >= 1 else fit
    physical_width, physical_height = width * scale, height * scale
    left = min(
        max(
            left_edge, round(rectangle.center().x() * pixel_ratio - physical_width / 2)
        ),
        right_edge - physical_width,
    )
    top = min(
        max(
            top_edge, round(rectangle.center().y() * pixel_ratio - physical_height / 2)
        ),
        bottom_edge - physical_height,
    )
    return QtCore.QRectF(
        left / pixel_ratio,
        top / pixel_ratio,
        physical_width / pixel_ratio,
        physical_height / pixel_ratio,
    )


class CaptionEditor(QtWidgets.QLineEdit):
    """Use Qt for input while leaving the firmware glyphs visible underneath."""

    def __init__(self, parent: QtWidgets.QWidget, blink_interval: int) -> None:
        super().__init__(parent)
        self.glyphs: list[QtCore.QRectF] = []
        self.drag_anchor: int | None = None
        self.caret_visible = True
        self.blink_timer = QtCore.QTimer(self)
        self.blink_timer.setInterval(blink_interval)
        self.blink_timer.timeout.connect(self.blink_caret)
        self.cursorPositionChanged.connect(lambda _old, _new: self.reset_caret())
        self.selectionChanged.connect(self.reset_caret)

    def blink_caret(self) -> None:
        self.caret_visible = not self.caret_visible
        self.update()

    def reset_caret(self) -> None:
        self.caret_visible = True
        if self.hasFocus():
            self.blink_timer.start()
        self.update()

    def focusInEvent(self, event: QtGui.QFocusEvent) -> None:
        super().focusInEvent(event)
        self.reset_caret()

    def focusOutEvent(self, event: QtGui.QFocusEvent) -> None:
        super().focusOutEvent(event)
        self.blink_timer.stop()
        self.update()

    def paintEvent(self, event: QtGui.QPaintEvent) -> None:
        painter = QtGui.QPainter(self)
        painter.setPen(QtGui.QColor('#939ba9'))
        painter.drawRect(self.rect().adjusted(0, 0, -1, -1))
        if self.hasSelectedText():
            self.paint_selection(painter)
        elif self.hasFocus() and self.caret_visible:
            x = 1
            if self.glyphs:
                position = self.cursorPosition()
                glyph = self.glyphs[min(position, len(self.glyphs) - 1)]
                x = round(
                    glyph.left() if position < len(self.glyphs) else glyph.right()
                )
            caret_width = 2
            x = min(max(1, x), self.width() - 1 - caret_width)
            painter.fillRect(
                x, 1, caret_width, self.height() - 2, QtGui.QColor('#d73737')
            )
        painter.end()

    def paint_selection(self, painter: QtGui.QPainter) -> None:
        selected = self.glyphs[self.selectionStart() : self.selectionEnd()]
        if not selected:
            return
        fill = QtGui.QColor(66, 122, 224, 85)
        painter.setClipRect(self.rect().adjusted(1, 1, -1, -1))
        bounds = selected[0]
        previous = selected[0]
        for glyph in selected[1:]:
            # Wrapped text moves back toward the left edge on the next row.
            if glyph.left() <= previous.left() and glyph.top() >= previous.bottom():
                painter.fillRect(bounds, fill)
                bounds = glyph
            else:
                bounds = bounds.united(glyph)
            previous = glyph
        painter.fillRect(bounds, fill)

    def index_at(self, point: QtCore.QPointF) -> int:
        if not self.glyphs:
            return self.cursorPosition()
        index = min(
            range(len(self.glyphs)),
            key=lambda i: (
                max(
                    self.glyphs[i].top() - point.y(),
                    point.y() - self.glyphs[i].bottom(),
                    0,
                ),
                abs(self.glyphs[i].center().x() - point.x()),
            ),
        )
        return index + int(point.x() >= self.glyphs[index].center().x())

    def mousePressEvent(self, event: QtGui.QMouseEvent) -> None:
        if event.button() == QtCore.Qt.MouseButton.LeftButton:
            self.drag_anchor = self.index_at(event.position())
            self.setCursorPosition(self.drag_anchor)
            event.accept()
        else:
            super().mousePressEvent(event)

    def mouseMoveEvent(self, event: QtGui.QMouseEvent) -> None:
        if self.drag_anchor is not None:
            self.setSelection(
                self.drag_anchor, self.index_at(event.position()) - self.drag_anchor
            )
            event.accept()
        else:
            super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event: QtGui.QMouseEvent) -> None:
        self.drag_anchor = None
        super().mouseReleaseEvent(event)


class ControllerCanvas(QtWidgets.QWidget):
    def __init__(
        self, session: Session, submit: Callable[[Callable[[], None]], None]
    ) -> None:
        super().__init__()
        self.session = session
        self.submit = submit
        self.doom_controls = False
        self.frames = session.frames.copy()
        self.editing_name = session.editing_name
        self.edit_caption: Callable[[QtCore.QPointF], bool] | None = None
        self.editable_caption_at: Callable[[QtCore.QPointF], bool] | None = None
        self.caption_geometry_changed: Callable[[], None] | None = None
        self.tooltip_context = session.tooltip_context, session.language
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
        self.update_minimum_size()
        self.setFocusPolicy(QtCore.Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)
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

    def update_minimum_size(self) -> None:
        layout = self.session.layout
        scale = 0.0
        ratio = self.devicePixelRatioF()
        for display in self.session.target.displays:
            placement = layout.displays[display.id]
            # Rounding both bezel edges inward can cost one physical pixel.
            scale = max(
                scale,
                (display.width + 1) / (placement.width * ratio),
                (display.height + 1) / (placement.height * ratio),
            )
        self.setMinimumSize(
            max(400, ceil(layout.width * scale)), max(260, ceil(layout.height * scale))
        )

    def update_tooltip_context(self) -> None:
        context = self.session.tooltip_context, self.session.language
        if context != self.tooltip_context:
            self.tooltip_context = context
            QtWidgets.QToolTip.hideText()

    def event(self, event: QtCore.QEvent) -> bool:
        if event.type() == QtCore.QEvent.Type.DevicePixelRatioChange:
            self.update_minimum_size()
        if (
            isinstance(event, QtGui.QHelpEvent)
            and event.type() == QtCore.QEvent.Type.ToolTip
        ):
            text, area = self.tooltip_at(event.pos())
            if text:
                body = escape(text).replace('\n', '<br>')
                QtWidgets.QToolTip.showText(
                    event.globalPos(), f'<p>{body}</p>', self, area
                )
            else:
                QtWidgets.QToolTip.hideText()
                event.ignore()
            return True
        return super().event(event)

    def tooltip_at(self, position: QtCore.QPoint) -> tuple[str, QtCore.QRect]:
        (focused, screens), language = self.tooltip_context
        translations = tooltip_messages(language)
        for display, (page, window) in zip(
            self.session.target.displays, screens, strict=False
        ):
            area = self.placement_rect(
                self.session.layout.displays[display.id]
            ).toRect()
            if area.contains(position):
                return translations.get(page, {}).get(
                    f'window-{window}', ''
                ).strip(), area
        identity = Canvas(self.session.layout, self.width(), self.height()).control(
            position.x(), position.y()
        )
        if identity is not None and any(
            c.id == identity and isinstance(c, models.Button)
            for c in self.session.controller.controls
        ):
            area = self.placement_rect(self.session.layout.controls[identity]).toRect()
            return translations.get(focused, {}).get(
                f'button-{identity}', ''
            ).strip(), area
        return '', QtCore.QRect()

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
        if self.caption_geometry_changed is not None:
            self.caption_geometry_changed()

    def caption_rect(self, region: TranslationRegion) -> QtCore.QRectF:
        display = self.session.target.displays[region.screen]
        return self.caption_pixel_rect(
            region.screen, region.left, region.top, region.right, region.bottom
        ).intersected(self.placement_rect(self.session.layout.displays[display.id]))

    def caption_pixel_rect(
        self, screen: int, left: int, top: int, right: int, bottom: int
    ) -> QtCore.QRectF:
        display = self.session.target.displays[screen]
        placement = self.placement_rect(self.session.layout.displays[display.id])
        image = display_rectangle(
            placement, display.width, display.height, self.devicePixelRatioF()
        )
        scale = image.width() / display.width
        return QtCore.QRectF(
            image.left() + left * scale,
            image.top() + top * scale,
            (right - left) * scale,
            (bottom - top) * scale,
        )

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
            if (pixels := self.frames.get(d.id)) and len(
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
        if not self.doom_controls:
            for n in sorted(self.active ^ active):
                self.submit(partial(self.session.button, n, n in active))
        self.active = active
        self.update()

    def mousePressEvent(self, event: QtGui.QMouseEvent) -> None:
        if (
            self.edit_caption is not None
            and event.button() == QtCore.Qt.MouseButton.LeftButton
            and self.edit_caption(event.position())
        ):
            event.accept()
            return
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

    def mouseMoveEvent(self, event: QtGui.QMouseEvent) -> None:
        if self.editable_caption_at is not None and self.editable_caption_at(
            event.position()
        ):
            self.setCursor(QtCore.Qt.CursorShape.IBeamCursor)
        else:
            self.unsetCursor()
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event: QtGui.QMouseEvent) -> None:
        if event.button() == QtCore.Qt.MouseButton.LeftButton:
            self.pointer.clear()
            self.synchronize()

    def keyPressEvent(self, event: QtGui.QKeyEvent) -> None:
        if event.isAutoRepeat():
            return
        if self.editing_name:
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
                    self.submit(partial(self.session.key, keys[event.key()]))
                else:
                    for c in event.text():
                        if 32 <= ord(c) <= 126:
                            self.submit(partial(self.session.key, ord(c)))
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
        self.submit(partial(self.session.absolute, control.id, position))

    def relative(self, identity: str, value: int) -> None:
        previous = self.relative_positions.get(identity, 0)
        self.relative_positions[identity] = value
        self.submit(partial(self.session.relative, identity, value - previous))


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
    request_done = QtCore.Signal()

    def __init__(
        self,
        session: Session,
        *,
        caret_blink_interval: int = 500,
        doom_host: Path | None = None,
    ) -> None:
        super().__init__()
        self.session = session
        self.doom_host = doom_host
        self.doom_process: doom.DoomHost | None = None
        self.doom_display = ''
        self.package_directory = Path(__file__).resolve().parents[1]
        self.worker = ThreadPoolExecutor(
            max_workers=1, thread_name_prefix='cntrl-native'
        )
        self.requests: deque[
            tuple[Callable[[], None], bool, Callable[[], None] | None]
        ] = deque()
        self.request: Future[None] | None = None
        self.request_done.connect(
            self.finish_request, QtCore.Qt.ConnectionType.QueuedConnection
        )
        self.stopped = session.failed or session.firmware_exited
        self.closing = False
        self.closing_session = False
        self.session_closed = False
        self.time_us = session.time_us
        self.language = session.language
        self.setLocale(QtCore.QLocale(self.language))
        self.translations = messages(self.language)
        self.dialog_translator = DialogTranslator(self)
        QtWidgets.QApplication.installTranslator(self.dialog_translator)
        self.captions: list[tuple[Callable[[str], None], str, dict[str, object]]] = []
        self.log = QtWidgets.QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.document().setMaximumBlockCount(2000)
        self.canvas = ControllerCanvas(session, self.submit)
        self.canvas.edit_caption = self.open_correction
        self.canvas.editable_caption_at = lambda point: (
            self.correction_at(point) is not None
        )
        self.canvas.caption_geometry_changed = self.position_correction
        self.correction_batch: CorrectionBatch | None = None
        self.correction_regions: list[TranslationRegion] = []
        self.correction_language_name = ''
        self.correction_region: TranslationRegion | None = None
        self.correction_initial = ''
        self.correction_position = QtCore.QRect()
        self.correction_dialog_open = False
        self.correction_language_prompt = False
        self.correction_editor = CaptionEditor(self.canvas, caret_blink_interval)
        self.correction_editor.setFrame(False)
        self.correction_editor.setStyleSheet(
            'QLineEdit { background: transparent; color: transparent; '
            'selection-color: transparent; selection-background-color: #40609060; '
            'border: 1px solid #939ba9; }'
        )
        self.correction_editor.hide()
        self.correction_editor.returnPressed.connect(self.accept_correction)
        self.correction_editor.textEdited.connect(self.preview_correction)
        self.correction_cancel = QtWidgets.QPushButton('×', self.canvas)
        self.correction_accept = QtWidgets.QPushButton('■', self.canvas)
        for button, key, color, padding_side, callback in (
            (
                self.correction_cancel,
                'cancel',
                '#d73737',
                'left',
                self.cancel_correction,
            ),
            (self.correction_accept, 'ok', '#148b3f', 'right', self.accept_correction),
        ):
            button.setAccessibleName(self.text(key))
            self.captions.append((button.setAccessibleName, key, {}))
            button.setStyleSheet(
                'QPushButton { background: transparent; border: none; '
                f'color: {color}; font-weight: bold; padding-{padding_side}: 8px; }}'
                'QPushButton:disabled { color: palette(mid); }'
            )
            button.setFixedSize(22, 22)
            button.setFocusPolicy(QtCore.Qt.FocusPolicy.NoFocus)
            button.clicked.connect(callback)
            button.hide()
        cancel_font = self.correction_cancel.font()
        if cancel_font.pixelSize() > 0:
            cancel_font.setPixelSize(round(cancel_font.pixelSize() * 1.25))
        else:
            cancel_font.setPointSizeF(cancel_font.pointSizeF() * 1.25)
        self.correction_cancel.setFont(cancel_font)
        self.correction_editor.installEventFilter(self)
        self.correction_preview_timer = QtCore.QTimer(self)
        self.correction_preview_timer.setSingleShot(True)
        self.correction_preview_timer.setInterval(45)
        self.correction_preview_timer.timeout.connect(self.send_correction_preview)
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
        self.recording = self.caption(QtWidgets.QCheckBox(), 'record_replay')
        self.recording.toggled.connect(self.record_replay)
        self.recording.setEnabled(not self.stopped)
        time_controls.addWidget(self.recording)
        self.replay = self.caption(QtWidgets.QPushButton(), 'replay')
        self.replay.clicked.connect(self.save_replay)
        self.replay.setEnabled(False)
        time_controls.addWidget(self.replay)
        self.clock_label = QtWidgets.QLabel()
        time_controls.addWidget(self.clock_label)
        column.addLayout(time_controls)
        correction_controls = QtWidgets.QHBoxLayout()
        self.correction_mode = self.caption(QtWidgets.QCheckBox(), 'correction_mode')
        self.correction_mode.toggled.connect(self.toggle_correction)
        correction_controls.addWidget(self.correction_mode)
        self.correction_actions = QtWidgets.QWidget()
        policy = self.correction_actions.sizePolicy()
        policy.setRetainSizeWhenHidden(True)
        self.correction_actions.setSizePolicy(policy)
        actions = QtWidgets.QHBoxLayout(self.correction_actions)
        actions.setContentsMargins(0, 0, 0, 0)
        self.correction_count = QtWidgets.QLabel()
        self.correction_buttons = [
            self.caption(QtWidgets.QPushButton(), key)
            for key in (
                'contribute_corrections',
                'save_corrections',
                'discard_corrections',
            )
        ]
        for button, callback in zip(
            self.correction_buttons,
            (
                self.contribute_corrections,
                self.save_corrections,
                self.discard_corrections,
            ),
            strict=True,
        ):
            actions.addWidget(button)
            button.clicked.connect(callback)
        actions.addWidget(self.correction_count)
        correction_controls.addWidget(self.correction_actions)
        correction_controls.addStretch()
        column.addLayout(correction_controls)
        self.update_correction_controls()
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

    def update_correction_controls(self) -> None:
        count = len(self.correction_batch.entries) if self.correction_batch else 0
        self.correction_actions.setVisible(self.correction_mode.isChecked())
        self.correction_count.setText(self.text('corrections_count', count=count))
        for button in self.correction_buttons:
            button.setEnabled(bool(count))

    def toggle_correction(self, enabled: bool) -> None:
        if enabled:
            self.submit(partial(self.session.capture_corrections, True))
        elif self.resolve_corrections():
            self.leave_corrections()
        else:
            with QtCore.QSignalBlocker(self.correction_mode):
                self.correction_mode.setChecked(True)
            if self.correction_region is not None:
                self.correction_editor.setFocus()
        self.update_correction_controls()

    def correction_at(self, point: QtCore.QPointF) -> TranslationRegion | None:
        if self.correction_mode.isChecked():
            return next(
                (
                    r
                    for r in reversed(self.correction_regions)
                    if self.canvas.caption_rect(r).contains(point)
                ),
                None,
            )
        return None

    def open_correction(self, point: QtCore.QPointF) -> bool:
        region = self.correction_at(point)
        if region is None:
            return False
        if self.correction_region is not None:
            self.correction_editor.setFocus()
            return True
        if self.correction_batch is None:
            self.correction_batch = CorrectionBatch(
                self.language, self.correction_language_name
            )
        accepted = self.correction_batch.entries.get((region.message, region.field))
        self.correction_initial = accepted.replacement if accepted else region.original
        self.correction_region = region
        self.correction_position = self.canvas.caption_rect(region).toAlignedRect()
        self.correction_editor.setText(region.editor_text(self.correction_initial))
        self.correction_editor.setAccessibleName(region.editor_text(region.original))
        self.position_correction()
        QtWidgets.QToolTip.hideText()
        self.correction_editor.show()
        self.correction_cancel.show()
        self.correction_accept.show()
        self.correction_editor.setFocus()
        return True

    def position_correction(self) -> None:
        region = self.correction_region
        if region is None:
            return
        # A longer draft can change the bounds or select the short form.
        for visible in self.correction_regions:
            if (
                visible.screen == region.screen
                and visible.message == region.message
                and visible.literal == region.literal
            ):
                region = visible
                break
        rectangle = self.canvas.caption_rect(region).toAlignedRect()
        if rectangle.isValid():
            self.correction_position = rectangle
        self.correction_editor.setGeometry(
            self.correction_position.adjusted(-1, -1, 1, 1)
        )
        offset = self.correction_editor.geometry().topLeft()
        self.correction_editor.glyphs = [
            self.canvas.caption_pixel_rect(region.screen, *g).translated(-offset)
            for g in region.glyphs
        ]
        self.correction_editor.update()
        x = self.correction_position.left()
        y = max(0, self.correction_position.top() - self.correction_cancel.height() - 3)
        self.correction_cancel.move(x, y)
        self.correction_accept.move(x + self.correction_cancel.width(), y)

    def eventFilter(self, watched: QtCore.QObject, event: QtCore.QEvent) -> bool:
        if (
            watched is self.correction_editor
            and isinstance(event, QtGui.QKeyEvent)
            and event.type() == QtCore.QEvent.Type.KeyPress
            and event.key() == QtCore.Qt.Key.Key_Escape
        ):
            self.cancel_correction()
            return True
        return super().eventFilter(watched, event)

    def correction_draft(self) -> Correction | None:
        region = self.correction_region
        if region is None:
            return None
        replacement = region.replace_literal(
            self.correction_initial, self.correction_editor.text()
        )
        if replacement == self.correction_initial:
            return None
        return Correction(
            message=region.message,
            field=region.field,
            original=region.original,
            replacement=replacement,
        )

    def preview_correction(self, text: str) -> None:
        self.correction_preview_timer.start()
        region = self.correction_region
        self.correction_accept.setEnabled(
            bool(text.strip())
            and region is not None
            and region.valid_replacement(
                region.replace_literal(self.correction_initial, text)
            )
        )

    def send_correction_preview(self) -> None:
        region = self.correction_region
        if region is None:
            return
        edited = self.correction_editor.text()
        replacement = region.replace_literal(self.correction_initial, edited)
        if edited.strip() and region.valid_replacement(replacement):
            self.submit(partial(self.session.preview_correction, region, replacement))

    def close_correction_editor(self) -> None:
        self.correction_preview_timer.stop()
        self.correction_region = None
        self.correction_editor.hide()
        self.correction_cancel.hide()
        self.correction_accept.hide()
        self.correction_accept.setEnabled(True)
        self.canvas.setFocus()

    def accept_correction(self) -> None:
        region, batch = self.correction_region, self.correction_batch
        if region is None or batch is None:
            return
        edited = self.correction_editor.text()
        text = region.replace_literal(self.correction_initial, edited)
        if edited.strip() and region.valid_replacement(text):
            self.send_correction_preview()
            batch.accept(region, text)
            self.close_correction_editor()
            self.update_correction_controls()
        else:
            self.correction_error(self.text('invalid_correction'))

    def cancel_correction(self) -> None:
        if self.correction_region is not None:
            self.submit(
                partial(
                    self.session.preview_correction,
                    self.correction_region,
                    self.correction_initial,
                )
            )
        self.close_correction_editor()

    def discard_corrections(self) -> None:
        self.close_correction_editor()
        self.correction_batch = None
        self.submit(self.session.clear_corrections)
        self.update_correction_controls()

    def leave_corrections(self) -> None:
        self.discard_corrections()
        with QtCore.QSignalBlocker(self.correction_mode):
            self.correction_mode.setChecked(False)
        self.correction_regions = []
        self.submit(partial(self.session.capture_corrections, False))
        self.update_correction_controls()

    def resolve_corrections(self) -> bool:
        if self.correction_dialog_open:
            return False
        if (
            not (self.correction_batch and self.correction_batch.entries)
            and self.correction_draft() is None
        ):
            return True
        self.correction_dialog_open = True
        try:
            dialog = QtWidgets.QMessageBox(self)
            dialog.setText(self.text('unsaved_corrections'))
            role = QtWidgets.QMessageBox.ButtonRole
            keep = dialog.addButton(self.text('continue_editing'), role.RejectRole)
            discard = dialog.addButton(self.text('discard'), role.DestructiveRole)
            save = dialog.addButton(self.text('save_more'), role.ActionRole)
            contribute = dialog.addButton(self.text('contribute_more'), role.ActionRole)
            dialog.setDefaultButton(keep)
            dialog.setEscapeButton(keep)
            dialog.exec()
            chosen = dialog.clickedButton()
            if chosen == discard:
                return True
            if chosen == save:
                return self.export_corrections(contribute=False)
            if chosen == contribute:
                return self.export_corrections(contribute=True)
            return False
        finally:
            self.correction_dialog_open = False

    def resolve_correction_language(self) -> None:
        dirty = bool(self.correction_batch and self.correction_batch.entries)
        dirty = dirty or self.correction_draft() is not None
        apply = self.resolve_corrections()
        if apply:
            if dirty:
                self.leave_corrections()
            else:
                self.discard_corrections()
        self.submit(
            partial(self.session.resolve_correction_language, apply),
            complete=self.finish_correction_language,
        )

    def finish_correction_language(self) -> None:
        self.correction_language_prompt = False

    def save_corrections(self) -> None:
        if self.export_corrections(contribute=False):
            self.leave_corrections()

    def contribute_corrections(self) -> None:
        if self.export_corrections(contribute=True):
            self.leave_corrections()

    def export_corrections(self, *, contribute: bool) -> bool:
        batch, draft = self.correction_batch, self.correction_draft()
        if batch is None:
            return False
        region = self.correction_region
        if (
            draft is not None
            and region is not None
            and (
                not self.correction_editor.text().strip()
                or not region.valid_replacement(draft.replacement)
            )
        ):
            self.correction_error(self.text('invalid_correction'))
            return False
        report = batch.report(draft)
        if contribute:
            body = issue_body(report)
            url = batch.issue_url(body)
            if len(url) > 8000:
                QtWidgets.QMessageBox.information(
                    self,
                    self.text('correction_mode'),
                    self.text('corrections_clipboard'),
                )
                clipboard = QtWidgets.QApplication.clipboard()
                clipboard.setText(body)
                if clipboard.text() != body:
                    self.correction_error(self.text('correction_export_failed'))
                    return False
                url = batch.issue_url()
            if QtGui.QDesktopServices.openUrl(QtCore.QUrl(url)):
                return True
            self.correction_error(self.text('correction_export_failed'))
            return False
        path = self.save_path(
            'save_corrections', f'corrections-{batch.language}.toml', 'toml'
        )
        if not path:
            return False
        if Path(path).suffix.lower() != '.toml':
            path += '.toml'
        output = QtCore.QSaveFile(path)
        data = report.encode()
        if (
            output.open(QtCore.QIODevice.OpenModeFlag.WriteOnly)
            and output.write(data) == len(data)
            and output.commit()
        ):
            return True
        self.correction_error(output.errorString())
        return False

    def correction_error(self, message: str) -> None:
        QtWidgets.QMessageBox.warning(self, self.text('correction_mode'), message)

    def caption[T: QtWidgets.QLabel | QtWidgets.QAbstractButton](
        self, widget: T, key: str, **fields: object
    ) -> T:
        widget.setText(self.text(key, **fields))
        if isinstance(widget, QtWidgets.QLabel):
            widget.setWordWrap(True)
        self.captions.append((widget.setText, key, fields))
        return widget

    def clock_mode(self) -> None:
        self.origin = monotonic_ns()
        self.origin_us = self.time_us

    def advance(self) -> None:
        self.running.setChecked(False)
        step_us = self.step.value() * 1000
        self.submit(
            lambda: self.session.advance(self.session.time_us + step_us),
            fatal=True,
        )

    def submit(
        self,
        operation: Callable[[], None],
        *,
        fatal: bool = False,
        complete: Callable[[], None] | None = None,
    ) -> None:
        if not self.stopped and not self.closing:
            self.requests.append((operation, fatal, complete))
            self.start_request()

    def start_request(self) -> None:
        if self.request is not None:
            return
        if self.requests:
            operation, self.request_fatal, self.request_complete = (
                self.requests.popleft()
            )
        elif self.closing and not self.closing_session:
            self.closing_session = True
            operation = self.close_session
            self.request_fatal = True
            self.request_complete = None
        else:
            return
        self.request = self.worker.submit(operation)
        self.request.add_done_callback(lambda future: self.request_done.emit())

    @QtCore.Slot()
    def finish_request(self) -> None:
        if self.request is None:
            return
        # The worker is idle until this callback finishes consuming session state.
        try:
            self.request.result()
        except (OSError, ValueError) as error:
            if self.request_fatal or self.session.failed:
                self.runtime_failed(error)
            else:
                self.log.appendPlainText(str(error))
        else:
            self.display_updates()
            if self.request_complete is not None:
                self.request_complete()
        self.request = None
        if self.closing_session:
            self.worker.shutdown(wait=False)
            self.finish_close()
        else:
            self.start_request()

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
                lambda state, endpoint=e.name: self.submit(
                    partial(self.session.connection, endpoint, state)
                )
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
                    lambda state, endpoint=e.name: self.submit(
                        partial(self.session.block, endpoint, state)
                    )
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
                    lambda value, index=port: self.submit(
                        partial(self.session.expression, index, value)
                    )
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
            self.submit(partial(self.session.auxiliary, port, mask))

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
            self.submit(partial(self.session.midi, endpoint, bytes.fromhex(text)))
        except ValueError as error:
            self.log.appendPlainText(str(error))

    def connect_host(self, endpoint: str) -> None:
        if self.stopped or self.closing:
            return
        if endpoint in self.host:
            self.release_inputs()

            def disconnected() -> None:
                self.close_host(endpoint)
                self.log.appendPlainText(
                    self.text('host_disconnected', endpoint=endpoint)
                )

            self.submit(self.session.cleanup, fatal=True, complete=disconnected)
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
        except MIDI_ERRORS as error:
            connection.close()
            self.log.appendPlainText(str(error))

    def tick(self) -> None:
        if self.doom_process is not None:
            self.doom_process.advance(
                {
                    c.name.upper()
                    for c in self.session.controller.controls
                    if c.id in self.canvas.active
                }
            )
            return
        if self.request is not None or self.stopped or self.closing:
            return
        if (status := self.session.poll_exit()) is not None:
            self.session.failed = True
            self.runtime_failed(
                ValueError(f'native emulator disconnected (exit {status})')
            )
            return
        if self.running.isChecked():
            time_us = self.origin_us + (monotonic_ns() - self.origin) // 1000
            self.submit(partial(self.session.advance, time_us, replay=True), fatal=True)
        for n, c in list(self.host.items()):
            try:
                for p in c.receive():
                    self.submit(partial(self.session.midi, n, p))
            except MIDI_ERRORS as error:
                self.host_failed(n, error)

    def runtime_failed(self, error: Exception) -> None:
        self.stopped = True
        self.requests.clear()
        self.timer.stop()
        self.running.setChecked(False)
        self.running.setEnabled(False)
        self.recording.setEnabled(False)
        self.close_host_connections()
        self.release_inputs()
        self.canvas.setEnabled(False)
        self.log.appendPlainText(self.text('stopped', error=error))

    def display_updates(self) -> None:
        if self.session.failed:
            self.close_host_connections()
            return
        self.time_us = self.session.time_us
        # A step can finish after the user resumes the live clock.
        elapsed_us = (monotonic_ns() - self.origin) // 1000
        self.origin_us = max(self.origin_us, self.time_us - elapsed_us)
        if not self.session.doom:
            self.canvas.frames = self.session.frames.copy()
        if self.correction_mode.isChecked():
            self.correction_regions = self.session.correction_regions
            self.correction_language_name = self.session.correction_language_name
            self.position_correction()
            if (
                self.session.requested_correction_language
                and not self.correction_language_prompt
            ):
                self.correction_language_prompt = True
                QtCore.QTimer.singleShot(0, self.resolve_correction_language)
        self.canvas.editing_name = self.session.editing_name
        self.canvas.update_tooltip_context()
        if self.language != self.session.language:
            self.language = self.session.language
            self.setLocale(QtCore.QLocale(self.language))
            self.translations = messages(self.language)
            for setter, key, fields in self.captions:
                setter(self.text(key, **fields))
            self.update_correction_controls()
        mode = self.text('mirror' if self.session.mode == 'pedal mirror' else 'preview')
        self.setWindowTitle(
            self.session.controller.name
            + f' [{mode}]'
            + (
                f' [{self.text("provisional")}]'
                if self.session.layout.provisional
                else ''
            )
            + f' | {self.package_directory}'
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
                    except MIDI_ERRORS as error:
                        self.host_failed(e.endpoint, error)
            elif e.kind == 'relay' and e.endpoint in self.relay_labels:
                self.relay_values[e.endpoint] = e.value
            elif e.kind == 'keyboard':
                self.log.appendPlainText(self.text('keyboard', data=e.data.hex(' ')))
            elif e.kind == 'firmware':
                self.log.appendPlainText(
                    e.message if self.session.doom else self.text('stock_message')
                )
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
        if self.session.firmware_exited:
            self.stopped = True
            self.requests.clear()
            self.timer.stop()
            self.running.setChecked(False)
            self.running.setEnabled(False)
            self.recording.setEnabled(False)
            if self.session.doom:
                if self.doom_process is None and not self.closing:
                    self.start_doom()
            else:
                self.canvas.setEnabled(False)
            self.clock_label.setText(
                'Doom' if self.session.doom else self.text('stock')
            )
            return
        self.clock_label.setText(f'{self.session.time_us / 1_000_000:.3f} s')
        self.canvas.update()

    def start_doom(self) -> None:
        self.canvas.frames.clear()
        self.canvas.update()
        self.canvas.setEnabled(False)
        if self.doom_host is None:
            self.log.appendPlainText(
                'Use --doom-host with a built MC6 Pro Doom demo to play.'
            )
            return
        displays = self.session.target.displays
        if len(displays) != 3 or any(
            (d.width, d.height) != (doom.WIDTH, doom.HEIGHT) for d in displays
        ):
            self.log.appendPlainText('Doom requires three 160 by 128 displays.')
            return
        left, right = doom_cards.control_cards()
        self.canvas.frames.update({displays[0].id: left, displays[2].id: right})
        self.doom_display = displays[1].id
        process = doom.DoomHost(self.doom_host, self)
        self.doom_process = process
        process.stopped.connect(self.stop_doom)
        process.frame.connect(self.present_doom)
        self.canvas.doom_controls = True
        self.canvas.editing_name = False
        self.canvas.release()
        self.canvas.setEnabled(True)
        self.canvas.setFocus()
        self.close_host_connections()
        self.timer.start(29)
        process.start()

    def present_doom(self, pixels: bytes) -> None:
        self.canvas.frames[self.doom_display] = pixels
        self.canvas.update()

    def stop_doom(self, message: str = '') -> None:
        process = self.doom_process
        if process is None:
            return
        self.doom_process = None
        self.timer.stop()
        self.canvas.setEnabled(False)
        if process.state() != QtCore.QProcess.ProcessState.NotRunning:
            process.kill()
            process.waitForFinished(1000)
        process.deleteLater()
        if message:
            self.log.appendPlainText(message)

    def host_failed(self, endpoint: str, error: BaseException) -> None:
        self.close_host(endpoint)
        self.connected[endpoint].setChecked(False)
        self.submit(self.session.cleanup, fatal=True)
        self.log.appendPlainText(
            self.text('host_failed', endpoint=endpoint, error=error)
        )

    def close_host(self, endpoint: str) -> None:
        if connection := self.host.pop(endpoint, None):
            try:
                connection.close()
            except MIDI_ERRORS as error:
                self.log.appendPlainText(
                    self.text('host_failed', endpoint=endpoint, error=error)
                )

    def close_host_connections(self) -> None:
        for endpoint in list(self.host):
            self.close_host(endpoint)

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

    def record_replay(self, enabled: bool) -> None:
        self.submit(
            partial(self.session.record_replay, enabled),
            complete=lambda: self.replay.setEnabled(True),
        )

    def save_replay(self) -> None:
        if self.closing:
            return
        path = self.save_path('replay_title', 'replay.toml', 'toml')
        if path:
            if self.stopped:
                try:
                    self.session.write_replay(Path(path))
                except OSError as error:
                    self.log.appendPlainText(str(error))
            else:
                self.submit(partial(self.session.write_replay, Path(path)))

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        if self.session_closed:
            event.accept()
            return
        event.ignore()
        if self.closing:
            return
        if self.correction_mode.isChecked():
            if not self.resolve_corrections():
                if self.correction_region is not None:
                    self.correction_editor.setFocus()
                return
            self.leave_corrections()
        self.timer.stop()
        self.stop_doom()
        self.release_inputs()
        self.closing = True
        self.start_request()

    def close_session(self) -> None:
        try:
            if not self.session.failed:
                self.session.cleanup()
        finally:
            self.session.close()

    def finish_close(self) -> None:
        self.session_closed = True
        self.close_host_connections()
        QtWidgets.QApplication.removeTranslator(self.dialog_translator)
        self.close()


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


def run_window(
    session: Session, *, caret_blink_interval: int = 500, doom_host: Path | None = None
) -> None:
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
            window.close_host_connections()
        try:
            previous_hook(kind, error, trace)
        finally:
            application.exit(1)

    sys.excepthook = unhandled
    try:
        window = EmulatorWindow(
            session, caret_blink_interval=caret_blink_interval, doom_host=doom_host
        )
        window.show()
        result = application.exec()
    finally:
        sys.excepthook = previous_hook
        if window is not None and not window.session_closed:
            window.timer.stop()
            window.stop_doom()
            window.closing = True
            window.requests.clear()
            window.worker.shutdown(cancel_futures=True)
            window.request = None
            try:
                session.close()
            finally:
                window.finish_close()
    if result:
        sys.exit(result)
