"""Compile Memon's settings into the shared firmware page record."""

import struct

from ..fonts.catalogue import UI_FONTS
from . import graphics, midi_endpoints, models
from .binary_format import DocumentFormatError


def pack(show: models.Setup, target: models.Target) -> bytes:
    if not any(p.type == 'memon' for p in show.pages.values()):
        return b''
    labels = {target.button_labels.get(b, b).upper() for b in target.buttons}
    if len(target.displays) != 3 or not set('ABCDEF') <= labels:
        raise ValueError('Memon requires three displays and switches A through F')
    settings = show.page_settings.memon or models.Memon()
    endpoint = next(
        (e for e in target.midi_endpoints if e.name == settings.device), None
    )
    if (
        endpoint is None
        or not midi_endpoints.enabled(endpoint, show)
        or (
            endpoint.port is not None
            and show.trs_ports.get(endpoint.name) not in ('midi-a', 'midi-b')
        )
    ):
        raise ValueError(
            f'page_settings.memon.device: unavailable output {settings.device!r}'
        )
    style = settings.style
    if style.font not in target.ui_fonts:
        raise ValueError(f'page_settings.memon.style: unavailable font {style.font!r}')
    chord = list(dict.fromkeys(settings.error_chord or settings.notes))
    flags = sum(
        int(v) << i
        for i, v in enumerate(
            (
                settings.show_remaining_time,
                settings.repeat_requests,
                settings.timed,
                settings.cooperative,
                bool(settings.error_chord),
                not settings.mono,
            )
        )
    )
    return CONFIG.pack(
        b'CMEM',
        1,
        endpoint.route,
        settings.channel,
        settings.velocity,
        flags,
        len(chord),
        settings.note_offset,
        UI_FONTS[style.font],
        style.color_offset,
        round(settings.note_duration * 1000),
        round(settings.gap_duration * 1000),
        round(settings.error_duration * 1000),
        round(settings.timeout * 1000),
        bytes(n + settings.note_offset for n in settings.notes),
        bytes(n + settings.note_offset for n in chord),
        bytes(
            c
            for v in [
                *style.tile_colors,
                style.foreground_color,
                style.background_color,
            ]
            for c in graphics.color(v)
        ),
    )


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CMEM':
        return offset
    if offset + CONFIG.size > len(data):
        raise DocumentFormatError('truncated Memon configuration')
    (
        _,
        version,
        route,
        channel,
        velocity,
        flags,
        count,
        base,
        font,
        _,
        note,
        _,
        error,
        _,
        notes,
        chord,
        _,
    ) = CONFIG.unpack_from(data, offset)
    if (
        version != 1
        or route >= 16
        or not 1 <= channel <= 16
        or not 1 <= velocity <= 127
        or flags > 63
        or not 1 <= count <= 4
        or base > 127
        or font not in UI_FONTS.values()
        or not note
        or not error
        or max(notes + chord) > 127
        or any(chord[count:])
        or len(set(chord[:count])) != count
    ):
        raise DocumentFormatError('invalid Memon configuration')
    return offset + CONFIG.size


def unpack(data: bytes, offset: int, names: dict[int, str]) -> dict[str, object] | None:
    if section_end(data, offset) == offset:
        return None
    (
        _,
        _,
        route,
        channel,
        velocity,
        flags,
        count,
        base,
        font,
        delta,
        note,
        gap,
        error,
        timeout,
        notes,
        chord,
        colors,
    ) = CONFIG.unpack_from(data, offset)
    if route not in names:
        raise DocumentFormatError('invalid Memon MIDI output')
    return dict(
        channel=channel,
        device=names[route],
        velocity=velocity,
        note_offset=base,
        notes=[n - base for n in notes],
        **({'error_chord': [n - base for n in chord[:count]]} if flags & 16 else {}),
        note_duration=note / 1000,
        gap_duration=gap / 1000,
        error_duration=error / 1000,
        timeout=timeout / 1000,
        show_remaining_time=bool(flags & 1),
        repeat_requests=bool(flags & 2),
        timed=bool(flags & 4),
        cooperative=bool(flags & 8),
        mono=not bool(flags & 32),
        style=dict(
            font=next(n for n, i in UI_FONTS.items() if i == font),
            color_offset=delta,
            tile_colors=[
                '0x' + colors[i : i + 3].hex().upper() for i in range(0, 12, 3)
            ],
            foreground_color='0x' + colors[12:15].hex().upper(),
            background_color='0x' + colors[15:].hex().upper(),
        ),
    )


CONFIG = struct.Struct('<4s9B4H4s4s18s')
