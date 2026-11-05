"""Compact runtime settings for the utility pages."""

import struct

from . import graphics, models


def pack(show: models.Setup, target: models.Target) -> list[int]:
    r = show.utility_pages
    if (
        r.title_height * 3 // 2
        + 4
        + r.tempo_height
        + r.flasher_diameter
        + 2 * r.annulus
        + 2 * r.padding
        > target.display_height
    ):
        raise ValueError(
            'utility_pages: title, tempo and flasher exceed display height'
        )
    if r.flasher_diameter + 2 * r.annulus + 2 * r.padding > target.display_width:
        raise ValueError('utility_pages: flasher exceeds display width')
    return [
        round(r.refresh_interval * 1000),
        round(r.clock_timeout * 1000),
        r.clocks_per_quarter,
        r.clock_average_beats,
        r.tap_intervals,
        r.minimum_taps,
        round(r.minimum_interval * 1000),
        round(r.reset_gap * 1000),
        round(r.flash_fraction * 1000),
        r.flasher_diameter,
        r.annulus,
        r.title_height,
        r.tempo_height,
        r.padding,
        8 if r.tempo_font == 'CourierPrime' else 9,
        ['classic', 'Arial', 'ComicSansMS'].index(r.status_font),
        graphics.rgb565(graphics.color(r.off_color)),
        graphics.rgb565(graphics.color(r.on_color)),
        graphics.rgb565(graphics.color(r.ring_color)),
        r.digits,
        r.decimals,
        graphics.rgb565(graphics.color(r.background_color)),
        ['classic', 'Arial', 'ComicSansMS'].index(r.title_font),
    ]


def pack_clock(show: models.Setup, target: models.Target) -> bytes:
    r = show.utility_pages
    if not r.clock_outputs and r.clock_source == 'internal' and r.clock_bpms == [120]:
        return b''
    if len(set(r.clock_outputs)) != len(r.clock_outputs):
        raise ValueError('utility_pages.clock_outputs: duplicate output')
    for name in r.clock_outputs:
        if (
            name not in CLOCK_ROUTES
            or name not in target.outputs
            or (
                (name == 'usb-device' and not show.usb_device_midi)
                or (name == 'usb-host' and not show.usb_host_midi)
            )
        ):
            raise ValueError(
                f'utility_pages.clock_outputs: unavailable output {name!r}'
            )
    return CLOCK.pack(
        sum(1 << CLOCK_ROUTES[n] for n in r.clock_outputs),
        int(r.clock_source == 'external'),
        *r.clock_bpms,
        *([0] * (4 - len(r.clock_bpms))),
    )


def unpack(data: bytes, offset: int) -> dict[str, object]:
    w = struct.unpack_from('<23H', data, offset)
    result: dict[str, object] = {}
    names = [
        'refresh_interval',
        'clock_timeout',
        'clocks_per_quarter',
        'clock_average_beats',
        'tap_intervals',
        'minimum_taps',
        'minimum_interval',
        'reset_gap',
        'flash_fraction',
        'flasher_diameter',
        'annulus',
        'title_height',
        'tempo_height',
        'padding',
    ]
    for i, n in enumerate(names):
        result[n] = w[i] / 1000 if i in (0, 1, 6, 7, 8) else w[i]
    result['tempo_font'] = 'CourierPrime' if w[14] == 8 else 'CourierPrimeBold'
    result['status_font'] = ['classic', 'Arial', 'ComicSansMS'][w[15]]
    result['title_font'] = ['classic', 'Arial', 'ComicSansMS'][w[22]]
    for i, n in (
        (16, 'off_color'),
        (17, 'on_color'),
        (18, 'ring_color'),
        (21, 'background_color'),
    ):
        r, g, b = w[i] >> 11, (w[i] >> 5) & 63, w[i] & 31
        result[n] = '0x{:02X}{:02X}{:02X}'.format(
            r * 255 // 31, g * 255 // 63, b * 255 // 31
        )
    result['digits'], result['decimals'] = w[19], w[20]
    return result


def unpack_clock(data: bytes, offset: int) -> dict[str, object]:
    mask, external, *slots = CLOCK.unpack_from(data, offset)
    return {
        'clock_outputs': [n for n, i in CLOCK_ROUTES.items() if mask & (1 << i)],
        'clock_source': 'external' if external else 'internal',
        'clock_bpms': [b for b in slots if b],
    }


def pack_expression(
    show: models.Setup, target: models.Target, jacks: tuple[str, ...]
) -> bytes:
    assignments = show.utility_pages.expression
    if not assignments:
        return b''
    data = bytearray(EXPRESSION.size * 4)
    for jack, assignment in assignments.items():
        if jack not in jacks or 'expression' not in target.trs_ports.get(jack, []):
            raise ValueError(f'expression jack {jack} unavailable on target')
        if show.trs_ports.get(jack) != 'expression':
            raise ValueError(f'expression jack {jack} requires the expression TRS role')
        if assignment.output not in target.outputs:
            raise ValueError(
                f'expression jack {jack}: unavailable output {assignment.output!r}'
            )
        if (
            assignment.output == 'usb-device'
            and not show.usb_device_midi
            or assignment.output == 'usb-host'
            and not show.usb_host_midi
        ):
            raise ValueError(
                f'expression jack {jack}: disabled output {assignment.output!r}'
            )
        name = assignment.output.encode('ascii')
        if not name or len(name) >= 32:
            raise ValueError('expression output name must fit the MIDI output record')
        EXPRESSION.pack_into(
            data,
            EXPRESSION.size * jacks.index(jack),
            name,
            assignment.channel,
            assignment.controller,
            assignment.minimum,
            assignment.maximum,
            assignment.reverse,
            CURVES.index(assignment.curve),
        )
    return bytes(data)


def unpack_expression(
    data: bytes, offset: int, jacks: tuple[str, ...]
) -> dict[str, object]:
    result: dict[str, object] = {}
    for i in range(4):
        output, channel, cc, low, high, reverse, curve = EXPRESSION.unpack_from(
            data, offset + i * EXPRESSION.size
        )
        if channel:
            result[jacks[i]] = dict(
                output=output.split(b'\0')[0].decode('ascii'),
                channel=channel,
                controller=cc,
                minimum=low,
                maximum=high,
                reverse=bool(reverse),
                curve=CURVES[curve],
            )
    return result


def pack_keyboard(show: models.Setup, target: models.Target) -> bytes:
    shortcuts = show.utility_pages.keyboard
    if not shortcuts:
        return b''
    if 'usb-hid-keyboard' not in target.functions:
        raise ValueError('USB HID keyboard unavailable on target')
    if len(shortcuts) > 65535:
        raise ValueError('keyboard shortcut list exceeds the setup format capacity')
    data = bytearray(2 + len(shortcuts) * KEYBOARD.size)
    struct.pack_into('<H', data, 0, len(shortcuts))
    for i, shortcut in enumerate(shortcuts):
        KEYBOARD.pack_into(
            data,
            2 + i * KEYBOARD.size,
            shortcut.name.encode('ascii'),
            shortcut.modifiers,
            *shortcut.keys,
            *([0] * (6 - len(shortcut.keys))),
        )
    return b'CKBD' + bytes(data)


def unpack_keyboard(data: bytes, offset: int) -> list[dict[str, object]]:
    return [
        dict(
            name=n.split(b'\0')[0].decode('ascii'),
            modifiers=m,
            keys=[v for v in k if v],
        )
        for n, m, *k in (
            KEYBOARD.unpack_from(data, offset + 2 + i * KEYBOARD.size)
            for i in range(struct.unpack_from('<H', data, offset)[0])
        )
    ]


EXPRESSION = struct.Struct('<32s6B')
EXPRESSION_FLAG = 1 << 15
CURVES = ('linear', 'slow', 'fast')


KEYBOARD = struct.Struct('<32s7B')


CLOCK_FLAG = 1 << 13
CLOCK = struct.Struct('<6H')
CLOCK_ROUTES = {
    'din': 0,
    'trs': 1,
    **{f'configurable-{i}': i + 1 for i in range(1, 5)},
    'usb-device': 6,
    'usb-host': 8,
}
