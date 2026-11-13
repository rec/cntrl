"""The shared live looper's configuration and bounded RAM2 allocation."""

import struct

from . import midi_endpoints, models


def pack(show: models.Setup, target: models.Target) -> bytes:
    settings = show.page_settings.looper
    if settings is None:
        if not any(p.type == 'midi-looper' for p in show.pages.values()):
            return b''
        settings = models.LooperSettings()
    channels, inputs, outputs = routing(settings, show, target)
    return CONFIG.pack(b'CLOP', 1, channels, inputs, outputs, settings.events)


def routing(
    settings: models.LooperSettings, show: models.Setup, target: models.Target
) -> tuple[int, int, int]:
    label = (
        'arpeggiator' if isinstance(settings, models.ArpeggiatorSettings) else 'looper'
    )
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
        raise ValueError(f'page_settings.{label}: target has no MIDI inputs')
    for n in inputs:
        if n not in incoming:
            raise ValueError(f'page_settings.{label}: unavailable input {n!r}')
    for n in outputs:
        if n not in outgoing:
            raise ValueError(f'page_settings.{label}: unavailable output {n!r}')
    return (
        sum(1 << (c - 1) for c in settings.channels),
        sum(1 << incoming[n] for n in inputs),
        sum(1 << outgoing[n] for n in outputs),
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
        or not 1 <= events <= 65535
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
    # Configuring an idle looper does not acquire capture or generation buffers.
    return 0


def estimate_arm_ram(data: bytes, *, capture_contexts: int | None = None) -> int:
    if not data:
        return 0
    _, _, channels, inputs, outputs, events = CONFIG.unpack(data)
    count = outputs.bit_count()
    contexts = count * channels.bit_count()
    if capture_contexts is None:
        capture_contexts = inputs.bit_count() * 16 * channels.bit_count()
    # One candidate SMF and index, merge scratch and order, held-note records,
    # parameter decoder contexts and shared delivery ownership credits.
    # Published and undo generations are accounted separately at runtime.
    return (
        events * 8
        + 64
        + 2 * events * 32
        + events * 4
        + (events // 2) * 16
        + capture_contexts * 56
        + 160 * (events // 2) * count
        + 72 * events
        + 132 * events * count
        + 396 * contexts
        + 76 * count
        + 6 * 16
        + 48
    )


CONFIG = struct.Struct('<4s5H')
