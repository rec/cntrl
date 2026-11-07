"""Portable shaping, sparse font assets, and per-show font budgets."""

import struct
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.fonts import assets
from cntrl.fonts.bitmap import BitmapRaster
from cntrl.fonts.outline import Outline, text_runs
from cntrl.patch_switcher import compiler, display, models


@pytest.fixture(scope='module')
def bitmap() -> bytes:
    return assets.pack(
        Outline(ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf'),
        {24: ['Café', 'office', 'é', *assets.NUMBERS]},
    )


def test_sparse_font_contains_only_requested_sizes_and_runs(bitmap: bytes) -> None:
    face = assets.decode(bitmap, 'Custom', crc32(bitmap))
    assert set(face.sizes) == {24}
    assert set(face.sizes[24].runs) == {'Café', 'office', 'é', *assets.NUMBERS}
    assert len(bitmap) < 4000
    fields, _, _ = assets.entries(bitmap)[24]
    assert struct.unpack('<H', bytes(fields[7:9]))[0] < 24


@pytest.mark.parametrize('kind', ['truncated', 'index', 'descriptor', 'extra'])
def test_malformed_fonts_are_rejected(bitmap: bytes, kind: str) -> None:
    data = bytearray(bitmap)
    if kind == 'truncated':
        data = data[:-1]
    elif kind == 'index':
        data[31:50] = bytes([255] * 19)
    elif kind == 'descriptor':
        data[9] = 1
    else:
        data += b'garbage'
    with pytest.raises(ValueError):
        assets.decode(bytes(data), 'Bad', crc32(data))


def test_selected_fonts_can_exceed_the_former_separate_budget(
    bitmap: bytes,
) -> None:
    key = crc32(bitmap)
    # Font bytes share the linked firmware heap rather than a separate 96 KiB cap.
    fields, index, pixels = assets.entries(bitmap)[24]
    fields[-1] += 100000
    large = (
        assets.HEADER.pack(b'CFN2', 1)
        + assets.ENTRY.pack(*fields)
        + index
        + pixels
        + bytes(100000)
    )
    records = [
        bytes([255, 24]) + bytes(10) + struct.pack('<I', k) for k in (key, key + 1)
    ]
    assert len(assets.entries(large)[24][2]) > 96 * 1024
    assets.validate_font_count(records, bytes(16))
    records = [bytes([255, 24]) + bytes(10) + struct.pack('<I', k) for k in range(33)]
    with pytest.raises(ValueError, match='32-font limit'):
        assets.validate_font_count(records, bytes(16))


def test_all_missing_glyphs_report_their_fonts_and_fields() -> None:
    source = compiler.read_document(ROOT / 'firmware/test/display.toml')
    assert isinstance(source, dict)
    source['patch_cycles'] = {
        'Group': [
            {'type': 'program-change', 'instrument': 'a', 'program': 1, 'name': '☃☂'},
            {'type': 'program-change', 'instrument': 'a', 'program': 2, 'name': '♠'},
        ]
    }
    outline = str(ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf')
    source['fonts'] = {'First': outline, 'Second': outline}
    source['style'] = {'font_name': 'First'}
    source['patch_views'] = {
        'patch': {
            'fields': ['display_name', 'other'],
            'line_styles': {'other': {'font_name': 'Second'}},
        }
    }
    source['parameters'] = {'other': {'value': '♣'}}
    source['windows'] = {}
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    font_data: dict[int, bytes] = {}
    with pytest.raises(compiler.CompilationError) as failure:
        compiler.compile_show(
            source,
            compiler.read_document(ROOT / 'firmware/test/definitions.json'),
            [],
            target,
            font_data,
        )
    message = str(failure.value)
    for t in [
        'First',
        'Second',
        'U+2603',
        'U+2602',
        'U+2660',
        'U+2663',
        'patch_cycles.Group[1]',
        'patch_cycles.Group[0]',
        'field other',
        'field display_name',
    ]:
        assert t in message
    assert not font_data


def test_shaping_retains_ligatures_and_normalizes_combining_accents() -> None:
    outline = Outline(ROOT / 'cntrl/fonts/caveat/Caveat-Bold.ttf')
    assert outline.shape(24, 'é') == outline.shape(24, 'e\u0301')
    assert len(outline.shape(24, 'fi').positions) == 1
    assert len(outline.shape(24, 'office').positions) < len('office')
    assert [t[:3] for t in text_runs('abc אבג 12')] == [
        (0, 4, 0),
        (8, 10, 2),
        (4, 8, 1),
    ]


def test_adding_text_requires_the_original_outline(
    tmp_path: Path, bitmap: bytes
) -> None:
    path = tmp_path / 'subset.cfont'
    path.write_bytes(bitmap)
    show = models.Setup.model_validate(
        {'instruments': {}, 'patch_cycles': {}, 'fonts': {'Custom': str(path)}}
    )
    with pytest.raises(ValueError, match='original font file'):
        assets.Preparation(
            show,
            models.Target.model_validate(
                compiler.read_document(ROOT / 'firmware/test/target.json')
            ),
            [(255, 'Never prepared', models.Style(font_name='Custom'), 4, 1, 0)],
            ['field title'],
        )


def test_backgrounds_do_not_request_unused_label_glyphs() -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    show = models.Setup.model_validate(
        {
            'instruments': {},
            'patch_cycles': {},
            'patch_views': {'patch': {'fields': []}},
        }
    )
    windows, _, _ = display.compile_display(
        show,
        [display.DisplayPatch(values={'patch_cycle.name': '☃', 'display_name': '雪'})],
        target,
    )
    assert all(w[0:2] == bytes([0, 8]) for w in windows)


def test_outline_scaling_changes_width_without_changing_height() -> None:
    path = ROOT / 'cntrl/fonts/still-time/StillTime.otf'
    normal, wide = Outline(path), Outline(path, 1.25)
    for text in ['world', 'Igneous', 'BowedSaw']:
        first, second = normal.shape(32, text), wide.shape(32, text)
        assert second.width == pytest.approx(first.width * 1.25, abs=2)
        assert (second.top, second.bottom) == (first.top, first.bottom)
        assert len(first.positions) == len(second.positions)


@pytest.mark.parametrize(
    'source,text,descent',
    [
        ('ewert/Ewert-Regular.ttf', 'guitar', 9),
        ('graduate/Graduate-Regular.ttf', 'reed', 6),
        ('courier-prime/CourierPrime-Regular.ttf', 'gyp', 10),
    ],
)
def test_line_boxes_reserve_font_descent(source: str, text: str, descent: int) -> None:
    outline = Outline(ROOT / 'cntrl/fonts' / source)
    data = assets.pack(outline, {28: [text]})
    face = assets.decode(data, 'Test', crc32(data))
    metrics = face.sizes[28]
    assert metrics.line_height - metrics.top >= descent
    assert metrics.line_height - metrics.top >= outline.shape(28, text).bottom


def test_prepared_capitals_keep_space_below_the_baseline() -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    prepared = assets.Preparation(
        models.Setup(),
        target,
        [(0, 'guitar', models.Style(font_name='OrnamentalSlab'), 4, 3, 0)],
        ['guitar heading'],
    )
    face = next(f for f in prepared.target.fonts if f.name == 'OrnamentalSlab')
    metrics = face.sizes[28]
    assert metrics.line_height - metrics.top == 9


def test_dynamic_ascii_uses_hinted_advances_and_preserves_pixels(
    tmp_path: Path,
) -> None:
    source = (
        ROOT
        / 'cntrl/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-SemiBold.ttf'
    )
    outline = Outline(source, autohint=True)
    characters = ''.join(chr(c) for c in range(32, 127))
    data = assets.pack(
        outline,
        {12: list(characters)},
        runtime_characters=characters,
        hinted_advances=True,
        prepared_runs=False,
    )
    face = assets.decode(data, 'Page', crc32(data))
    assert set(face.sizes[12].characters) == set(characters)
    assert not face.sizes[12].runs
    path = tmp_path / 'page.cfont'
    path.write_bytes(data)
    raster = BitmapRaster(path)
    for c in characters:
        first = outline.glyph(12, outline.face.get_char_index(ord(c)))
        second = raster.glyph(12, ord(c))
        assert second.pixels == first.pixels
        assert (second.width, second.height, second.left, second.top) == (
            first.width,
            first.height,
            first.left,
            first.top,
        )
        assert second.advance == round(first.advance)


ROOT = Path(__file__).resolve().parents[1]
