"""Compact runtime settings for the auxiliary performance pages."""

import struct

from . import graphics, models


def pack(show: models.Setup, target: models.Target) -> list[int]:
    r = show.right_pages
    if (
        r.title_height * 3 // 2
        + 4
        + r.tempo_height
        + r.flasher_diameter
        + 2 * r.annulus
        + 2 * r.padding
        > target.display_height
    ):
        raise ValueError('right_pages: title, tempo and flasher exceed display height')
    if r.flasher_diameter + 2 * r.annulus + 2 * r.padding > target.display_width:
        raise ValueError('right_pages: flasher exceeds display width')
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
