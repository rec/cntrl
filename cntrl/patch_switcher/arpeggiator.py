"""Compile the pedal's beat-synchronized held and phrase arpeggiators."""

import struct
from fractions import Fraction
from math import lcm

from . import looper, models


def pack(show: models.Setup, target: models.Target) -> bytes:
    settings = show.page_settings.arpeggiator
    if settings is None:
        if not any(p.type == 'arpeggiator' for p in show.pages.values()):
            return b''
        settings = models.ArpeggiatorSettings()
    selection = settings.selection
    kind = selection.kind
    if 'selection' not in settings.model_fields_set and settings.mode == 'mono':
        kind = 'played'
    if (
        kind == 'played'
        and settings.mode != 'mono'
        or kind in ('ascending', 'descending')
        and settings.mode != 'poly'
    ):
        raise ValueError('arpeggiator selection does not match its bank mode')
    channels, inputs, outputs = looper.routing(settings, show, target)
    step = Fraction(settings.rhythm.step[:-5])
    gate = step * Fraction(settings.gate)
    flags, modulus = 0, 2
    up = down = Fraction(1, 2)
    if isinstance(selection, models.ArpeggiatorAlternating):
        flags = int(selection.repeat_begin) | int(selection.repeat_end) << 1
    elif isinstance(selection, models.ArpeggiatorPlayed):
        flags = int(selection.direction == 'reverse') << 3
    elif isinstance(selection, models.ArpeggiatorWalk):
        flags = int(selection.cycle) << 2
        up, down = Fraction(selection.prob_up), Fraction(selection.prob_down)
    elif isinstance(selection, models.ArpeggiatorMod):
        modulus = selection.modulus
    denominator = lcm(up.denominator, down.denominator)
    if denominator > 4294967295:
        raise ValueError('walk probability denominator exceeds 32 bits')
    return CONFIG.pack(
        b'CARP',
        1,
        settings.mode == 'mono',
        KINDS.index(kind),
        flags,
        channels,
        inputs,
        outputs,
        settings.events,
        modulus,
        max(1, round(step * (1 << 32))),
        max(1, round(gate * (1 << 32))),
        int(up * denominator),
        int(down * denominator),
        denominator,
        settings.seed,
    )


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CARP':
        return offset
    if offset + CONFIG.size > len(data):
        raise ValueError('truncated arpeggiator configuration')
    (
        _,
        version,
        mode,
        kind,
        flags,
        channels,
        inputs,
        outputs,
        events,
        modulus,
        step,
        gate,
        up,
        down,
        denominator,
        seed,
    ) = CONFIG.unpack_from(data, offset)
    if (
        version != 1
        or mode > 1
        or kind >= len(KINDS)
        or flags > 15
        or not channels
        or not inputs
        or not outputs
        or not 1 <= events <= 65535
        or modulus < 2
        or not 0 < gate <= step <= 64 << 32
        or not denominator
        or up + down > denominator
        or not seed
    ):
        raise ValueError('invalid arpeggiator configuration')
    return offset + CONFIG.size


def unpack(data: bytes, offset: int, names: dict[int, str]) -> dict[str, object] | None:
    if section_end(data, offset) == offset:
        return None
    (
        _,
        _,
        mode,
        kind,
        flags,
        channels,
        inputs,
        outputs,
        events,
        modulus,
        step,
        gate,
        up,
        down,
        denominator,
        seed,
    ) = CONFIG.unpack_from(data, offset)
    selection: dict[str, object] = {'kind': KINDS[kind]}
    if kind == 2:
        selection['direction'] = 'reverse' if flags & 8 else 'forward'
    elif kind == 3:
        selection.update(repeat_begin=bool(flags & 1), repeat_end=bool(flags & 2))
    elif kind == 5:
        selection.update(
            cycle=bool(flags & 4),
            prob_up=str(Fraction(up, denominator)),
            prob_down=str(Fraction(down, denominator)),
        )
    elif kind == 6:
        selection['modulus'] = modulus
    return dict(
        mode='mono' if mode else 'poly',
        selection=selection,
        inputs=[n for i, n in names.items() if inputs & (1 << i)],
        outputs=[n for i, n in names.items() if outputs & (1 << i)],
        channels=[i + 1 for i in range(16) if channels & (1 << i)],
        events=events,
        rhythm={'kind': 'grid', 'step': f'{Fraction(step, 1 << 32)} beat'},
        gate=str(Fraction(gate, step)),
        seed=seed,
    )


def estimate_ram(data: bytes) -> int:
    if not data:
        return 0
    _, _, mono, _, _, channels, _, outputs, events, *_ = CONFIG.unpack(data)
    count = outputs.bit_count()
    if mono:
        return 104 * count
    return (
        64 * events
        + 56 * events * count
        + 496 * count
        + 412 * count * channels.bit_count()
        + 16
    )


def estimate_arm_ram(data: bytes, *, capture_contexts: int | None = None) -> int:
    if not data:
        return 0
    _, _, mono, _, _, channels, inputs, outputs, events, *_ = CONFIG.unpack(data)
    if not mono:
        return 0
    count = outputs.bit_count()
    if capture_contexts is None:
        capture_contexts = inputs.bit_count() * 16 * channels.bit_count()
    # ARM32 checked-node sizes are asserted alongside the firmware record types.
    # Include 16 bytes per allocation for allocator metadata and alignment.
    return (
        224 * events
        + 140 * (events // 2)
        + 144 * events * count
        + 56 * capture_contexts
        + 412 * count * channels.bit_count()
        + 248 * count
        + 32
    )


KINDS = (
    'ascending',
    'descending',
    'played',
    'alternating',
    'choice',
    'walk',
    'mod',
    'shuffle',
    'outside_in',
)
CONFIG = struct.Struct('<4s4B5H2Q4I')
