"""The shared live looper's configuration and bounded RAM2 allocation."""

import struct

from . import midi_endpoints, models


def pack(show: models.Setup, target: models.Target) -> bytes:
    settings = show.page_settings.looper
    if settings is None:
        if not any(p.type == 'midi-looper' for p in show.pages.values()):
            return b''
        settings = models.LooperSettings()
    incoming = {
        e.name: e.route
        for e in target.midi_endpoints
        if e.input
        and (e.transport != 'usb-device' or show.usb_device_midi)
        and (e.transport != 'usb-host' or show.usb_host_midi)
    }
    outgoing = {
        e.name: e.route
        for e in target.midi_endpoints
        if e.output
        and midi_endpoints.enabled(e, show)
        and (e.port is None or show.trs_ports.get(e.name) in ('midi-a', 'midi-b'))
    }
    inputs = settings.inputs if settings.inputs is not None else list(incoming)
    outputs = settings.outputs or [target.default_midi_output]
    if not inputs:
        raise ValueError('page_settings.looper: target has no MIDI inputs')
    for n in inputs:
        if n not in incoming:
            raise ValueError(f'page_settings.looper: unavailable input {n!r}')
    for n in outputs:
        if n not in outgoing:
            raise ValueError(f'page_settings.looper: unavailable output {n!r}')
    return CONFIG.pack(
        b'CLOP',
        1,
        sum(1 << (c - 1) for c in settings.channels),
        sum(1 << incoming[n] for n in inputs),
        sum(1 << outgoing[n] for n in outputs),
        settings.events,
    )


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CLOP':
        return offset
    if offset + CONFIG.size > len(data):
        raise ValueError('truncated MIDI looper configuration')
    _, version, channels, inputs, outputs, events = CONFIG.unpack_from(data, offset)
    if (
        version != 1
        or not channels
        or not inputs
        or not outputs
        or not 1 <= events <= 4096
    ):
        raise ValueError('invalid MIDI looper configuration')
    return offset + CONFIG.size


def unpack(data: bytes, offset: int, names: dict[int, str]) -> dict[str, object] | None:
    if section_end(data, offset) == offset:
        return None
    _, _, channels, inputs, outputs, events = CONFIG.unpack_from(data, offset)
    return dict(
        inputs=[n for i, n in names.items() if inputs & (1 << i)],
        outputs=[n for i, n in names.items() if outputs & (1 << i)],
        channels=[i + 1 for i in range(16) if channels & (1 << i)],
        events=events,
    )


def estimate_ram(data: bytes) -> int:
    if not data:
        return 0
    _, _, _, _, _, events = CONFIG.unpack(data)
    # Three bounded SMFs, four prepared indexes (including merge scratch),
    # merge order and shared delivery ownership keys/claims.
    return (
        3 * (events * 8 + 64)
        + 4 * events * 32
        + events * 4
        + 36 * (128 + 32 * 16)
        + 1792
        + 6144
        + 2048
        + 256
    )


CONFIG = struct.Struct('<4s5H')
