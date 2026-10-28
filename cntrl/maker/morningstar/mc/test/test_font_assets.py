"""Custom fonts compile locally, survive transfer, and load transactionally."""

import os
import pty
import struct
import subprocess
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit

from cntrl.maker.morningstar.mc import font_assets
from cntrl.maker.morningstar.mc.sparse_fonts import Outline, text_runs
from cntrl.patch_switcher import cli, compiler, display, models, resolved, transport


@pytest.fixture(scope='module')
def bitmap() -> bytes:
    return font_assets.pack(
        Outline(ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf'),
        {24: ['Café', 'office', 'é', *font_assets.NUMBERS]},
    )


def test_switch_statuses_have_prepared_runs_and_unicode_labels() -> None:
    source = compiler.read_document(ROOT / 'firmware/test/show.json')
    assert isinstance(source, dict)
    source['switches'] = {
        'sustain': {
            'name': 'Résumé',
            'button': 'A',
            'mode': 'momentary',
            'style': {'font_name': 'CaveatBold'},
            'on': {
                'type': 'control-change',
                'output': 'din',
                'channel': 1,
                'controller': 64,
                'value': 127,
            },
            'off': {
                'type': 'control-change',
                'output': 'din',
                'channel': 1,
                'controller': 64,
                'value': 0,
            },
        }
    }
    assets: dict[int, bytes] = {}
    target = models.Target.model_validate(
        compiler.read_document(MC / 'patch-switcher-target.json')
    )
    data = compiler.compile_show(
        source,
        compiler.read_document(ROOT / 'firmware/test/definitions.json'),
        [],
        target,
        assets,
    )
    assert data and assets
    faces = [font_assets.decode(b, 'Actions', k) for k, b in assets.items()]
    for text in ['A: Résumé', *font_assets.SWITCH_STATES]:
        assert any(text in m.runs for f in faces for m in f.sizes.values())


@pytest.fixture(scope='module')
def peer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    executable = tmp_path_factory.mktemp('font-peer') / 'peer'
    firmware = MC / 'firmware'
    library = firmware / '.tools/data/packages/teensy/hardware/avr/1.62.0/libraries'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{ROOT / "firmware/include"}',
            f'-I{ROOT / "firmware/test"}',
            f'-I{firmware / "test_support/font_assets"}',
            f'-I{library / "ST7735_t3/src"}',
            str(firmware / 'test_font_assets.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    return executable


def test_relative_font_paths_compile_and_export_with_reusable_assets(
    tmp_path: Path,
) -> None:
    outline = ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf'
    source = compiler.read_document(ROOT / 'firmware/test/display.toml')
    assert isinstance(source, dict)
    source['fonts'] = {'Handwriting': 'custom.ttf'}
    source['style'] = {'font_name': 'Handwriting'}
    (tmp_path / 'custom.ttf').write_bytes(outline.read_bytes())
    document = tmp_path / 'show.toml'
    document.write_text(tomlkit.dumps(source))
    command = cli.Sources(
        document=[str(document)],
        target=[str(ROOT / 'firmware/test/target.json')],
        definitions=[str(ROOT / 'firmware/test/definitions.json')],
    )
    assets: dict[str, dict[int, bytes]] = {}
    data = cli.compile_inputs(command, assets)['show']
    assert assets['show']
    key, bitmap = next(iter(assets['show'].items()))
    assert data[28] == 0  # Window records only paint backgrounds.
    assert struct.pack('<I', key) in data
    exported = resolved.export(data)
    assert tomlkit.parse(exported).unwrap()['fonts'] == {
        'Handwriting': f'fonts/{key:08x}.cfont'
    }
    (tmp_path / 'fonts').mkdir()
    (tmp_path / 'fonts' / f'{key:08x}.cfont').write_bytes(bitmap)
    document.write_text(exported)
    assert cli.compile_inputs(command)['show'] == data


def test_sparse_font_contains_only_requested_sizes_and_runs(bitmap: bytes) -> None:
    face = font_assets.decode(bitmap, 'Custom', crc32(bitmap))
    assert set(face.sizes) == {24}
    assert set(face.sizes[24].runs) == {'Café', 'office', 'é', *font_assets.NUMBERS}
    assert len(bitmap) < 4000
    fields, _, _ = font_assets.entries(bitmap)[24]
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
        font_assets.decode(bytes(data), 'Bad', crc32(data))


def test_asset_transfer_and_failed_load_preserve_active_fonts(
    peer: Path, bitmap: bytes, tmp_path: Path
) -> None:
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer), str(tmp_path)], stdin=master, stdout=master)
    key = crc32(bitmap)
    try:
        with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
            client = transport.SetupClient(connection)
            client.copy_font(key, bitmap)
            path = tmp_path / 'cntrl/fonts' / f'{key:08x}.cfont'
            assert path.read_bytes() == bitmap
            stamp = path.stat().st_mtime_ns
            client.copy_font(key, bitmap)
            assert path.stat().st_mtime_ns == stamp
            client.ok(f'STAGE {key:08x} 24')
            client.ok(f'ACTIVE {key:08x} 24')
            with pytest.raises(ValueError, match='font-load'):
                client.ok(f'STAGE {key:08x} 13')
            client.ok(f'ACTIVE {key:08x} 24')
            malformed = bitmap[:-1]
            bad_key = crc32(malformed)
            with pytest.raises(ValueError, match='font-asset'):
                client.copy_font(bad_key, malformed)
            assert not (path.parent / f'{bad_key:08x}.cfont').exists()
            client.ok(f'ACTIVE {key:08x} 24')
            # A power interruption leaves the old committed asset intact.
            assert (
                client.request(f'CNTRL FONT BEGIN {bad_key:08x} {len(bitmap)}')
                == 'CNTRL SEND'
            )
            client.ok('CNTRL FONT DATA ' + bitmap[:128].hex())
            client.copy_font(key, bitmap)
            client.ok(f'ACTIVE {key:08x} 24')
            broken = bytearray(bitmap)
            broken[-1] ^= 1
            path.write_bytes(broken)
            with pytest.raises(ValueError, match='font-load'):
                client.ok(f'STAGE {key:08x} 24')
            client.ok(f'ACTIVE {key:08x} 24')
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_show_activation_requires_assets_and_releases_previous_fonts(
    peer: Path, bitmap: bytes, tmp_path: Path
) -> None:
    source = compiler.read_document(ROOT / 'firmware/test/display.toml')
    assert isinstance(source, dict)
    source['fonts'] = {
        'Custom': str(ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf')
    }
    source['style'] = {'font_name': 'Custom'}
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    definitions = compiler.read_document(ROOT / 'firmware/test/definitions.json')
    assets: dict[int, bytes] = {}
    data = compiler.compile_show(source, definitions, [], target, assets)
    key, bitmap = next(iter(assets.items()))
    size = next(iter(font_assets.entries(bitmap)))
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer), str(tmp_path)], stdin=master, stdout=master)
    try:
        with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
            client = transport.SetupClient(connection)
            with pytest.raises(ValueError, match='invalid-document'):
                client.copy('Custom', data, False)
            client.copy_font(key, bitmap)
            client.copy('Custom', data, False)
            client.ok('CNTRL SETUP FINISH')
            assert client.current() == 'Custom'
            client.ok(f'ACTIVE {key:08x} {size}')
            plain = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
            client.copy('Plain', plain, False)
            client.select('Plain')
            with pytest.raises(ValueError, match='absent'):
                client.ok(f'ACTIVE {key:08x} {size}')
            client.select('Custom')
            client.ok(f'ACTIVE {key:08x} {size}')
            # Corruption on SD must prevent selection without replacing the
            # active document or the already loaded pixels.
            path = tmp_path / 'cntrl/fonts' / f'{key:08x}.cfont'
            path.write_bytes(bitmap[:-1] + bytes([bitmap[-1] ^ 1]))
            with pytest.raises(ValueError, match='selection-failed'):
                client.select('Custom')
            assert client.current() == 'Custom'
            client.ok(f'ACTIVE {key:08x} {size}')
            assert client.download('Custom') == data
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_rejects_selected_fonts_that_exceed_the_loaded_ram_budget(
    bitmap: bytes,
) -> None:
    key = crc32(bitmap)
    # Exercise the combined budget independently of individual file validation.
    fields, index, pixels = font_assets.entries(bitmap)[24]
    fields[-1] += 48000
    large = (
        font_assets.HEADER.pack(b'CFN2', 1)
        + font_assets.ENTRY.pack(*fields)
        + index
        + pixels
        + bytes(48000)
    )
    records = [
        bytes([255, 24]) + bytes(10) + struct.pack('<I', k) for k in (key, key + 1)
    ]
    with pytest.raises(ValueError, match='96 KiB RAM budget'):
        font_assets.validate_budget(records, bytes(16), {key: large, key + 1: large})


ROOT = Path(__file__).resolve().parents[5]
MC = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize('function', [False, True])
def test_unicode_labels_compile_transfer_and_export(
    peer: Path, tmp_path: Path, function: bool
) -> None:
    source = compiler.read_document(ROOT / 'firmware/test/display.toml')
    assert isinstance(source, dict)
    source['groups'] = {
        'Été': [
            {
                'type': 'program-change',
                'instrument': 'a',
                'program': 1,
                'name': 'Café e\u0301',
            }
        ]
    }
    source['fonts'] = {
        'Custom': str(ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf')
    }
    source['style'] = {'font_name': 'Custom'}
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    if function:
        source['groups']['Été'].append(
            {
                'exit_buttons': 'A+F',
                'display_name': 'Été original',
                'type': 'stock-firmware',
            }
        )
        target = target.model_copy(
            update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')}
        )
    assets: dict[int, bytes] = {}
    data = compiler.compile_show(
        source,
        compiler.read_document(ROOT / 'firmware/test/definitions.json'),
        [],
        target,
        assets,
    )
    assert 'Café e\u0301' in resolved.export(data)
    master, slave = pty.openpty()
    process = subprocess.Popen([str(peer), str(tmp_path)], stdin=master, stdout=master)
    try:
        with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
            client = transport.SetupClient(connection)
            for k, b in assets.items():
                client.copy_font(k, b)
            client.copy('Unicode', data, False)
            client.ok('CNTRL SETUP FINISH')
            assert client.download('Unicode') == data
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


def test_all_missing_glyphs_report_their_fonts_and_fields() -> None:
    source = compiler.read_document(ROOT / 'firmware/test/display.toml')
    assert isinstance(source, dict)
    source['groups'] = {
        'Group': [
            {'type': 'program-change', 'instrument': 'a', 'program': 1, 'name': '☃☂'},
            {'type': 'program-change', 'instrument': 'a', 'program': 2, 'name': '♠'},
        ]
    }
    outline = str(ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf')
    source['fonts'] = {'First': outline, 'Second': outline}
    source['style'] = {'font_name': 'First'}
    source['pages'] = {
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
    assets: dict[int, bytes] = {}
    with pytest.raises(compiler.CompilationError) as failure:
        compiler.compile_show(
            source,
            compiler.read_document(ROOT / 'firmware/test/definitions.json'),
            [],
            target,
            assets,
        )
    message = str(failure.value)
    for t in [
        'First',
        'Second',
        'U+2603',
        'U+2602',
        'U+2660',
        'U+2663',
        'groups.Group[1]',
        'groups.Group[0]',
        'field other',
        'field display_name',
    ]:
        assert t in message
    assert not assets


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
    show = models.Show.model_validate(
        {'instruments': {}, 'groups': {}, 'fonts': {'Custom': str(path)}}
    )
    with pytest.raises(ValueError, match='original font file'):
        font_assets.Preparation(
            show,
            models.Target.model_validate(
                compiler.read_document(ROOT / 'firmware/test/target.json')
            ),
            [(255, 'Never prepared', models.Style(font_name='Custom'), 4, 1, 0)],
            ['field title'],
        )


def test_bundled_outline_fonts_use_sparse_unicode_assets() -> None:
    target = models.Target.model_validate(
        compiler.read_document(MC / 'patch-switcher-target.json')
    )
    show = models.Show(
        instruments={}, patches={}, style=models.Style(font_name='CourierPrime')
    )
    assets: dict[int, bytes] = {}
    _, _, block = display.compile_display(
        show,
        [display.DisplayPatch(values={'group.name': 'Été', 'display_name': 'Café'})],
        target,
        assets,
    )
    assert assets and block[display.HEADER.size] == 255
    for k, b in assets.items():
        face = font_assets.decode(b, 'CourierPrime', k)
        assert set().union(*(set(m.runs) for m in face.sizes.values())) == {
            'Été',
            'Café',
        }


def test_backgrounds_do_not_request_unused_label_glyphs() -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    show = models.Show.model_validate(
        {'instruments': {}, 'groups': {}, 'pages': {'patch': {'fields': []}}}
    )
    windows, _, _ = display.compile_display(
        show,
        [display.DisplayPatch(values={'group.name': '☃', 'display_name': '雪'})],
        target,
    )
    assert all(w[0:2] == bytes([0, 8]) for w in windows)
