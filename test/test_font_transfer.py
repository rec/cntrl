"""Custom fonts compile locally, survive transfer, and load transactionally."""

import struct
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit
from native_peer import NativePeer

from cntrl.fonts import assets
from cntrl.fonts.outline import Outline
from cntrl.patch_switcher import (
    cli,
    compiler,
    data_files,
    display,
    models,
    resolved,
    setup_bundle,
    transport,
)
from scripts import native_tests


@pytest.fixture(scope='module')
def bitmap() -> bytes:
    return assets.pack(
        Outline(ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf'),
        {24: ['Café', 'office', 'é', *assets.NUMBERS]},
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
    font_data: dict[int, bytes] = {}
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(
                name='target', paths=[ROOT / 'test/data/font-target.toml']
            ),
            False,
            [],
        )
    )
    data = compiler.compile_show(
        source,
        compiler.read_document(ROOT / 'firmware/test/definitions.json'),
        [],
        target,
        font_data,
    )
    assert data and font_data
    faces = [assets.decode(b, 'Actions', k) for k, b in font_data.items()]
    for text in ['A: Résumé', *assets.SWITCH_STATES]:
        assert any(text in m.runs for f in faces for m in f.sizes.values())


def test_autoformatting_prepares_every_bundled_outline_family() -> None:
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(
                name='target', paths=[ROOT / 'test/data/font-target.toml']
            ),
            False,
            [],
        )
    )
    source = compiler.read_document(ROOT / 'firmware/test/show.json')
    assert isinstance(source, dict)
    source['fill_in_styles'] = True
    source['patch_cycles'] = {
        f'CYCLE {i}': [
            {
                'type': 'program-change',
                'instrument': 'a',
                'program': i + 1,
                'name': 'Patch',
            }
        ]
        for i in range(len(target.fonts))
    }
    font_data: dict[int, bytes] = {}
    data = compiler.compile_show(
        source,
        compiler.read_document(ROOT / 'firmware/test/definitions.json'),
        [],
        target,
        font_data,
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert set(exported['fonts']) == {
        f.name if f.horizontal_scale == 1 else f'{f.name}@{f.horizontal_scale}x'
        for f in target.fonts[3:]
    }
    for name, path in exported['fonts'].items():
        key = int(Path(path).stem, 16)
        face = assets.decode(font_data[key], name, key)
        assert any('Patch' in m.runs for m in face.sizes.values())


@pytest.fixture(scope='module')
def peer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    executable = tmp_path_factory.mktemp('font-peer') / 'peer'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{ROOT / "firmware/include"}',
            f'-I{ROOT / "firmware/test"}',
            str(ROOT / 'firmware/test/font_server.cpp'),
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
    font_data: dict[str, dict[int, bytes]] = {}
    data = cli.compile_inputs(command, font_data).setups['show']
    assert font_data['show']
    key, bitmap = next(iter(font_data['show'].items()))
    assert data[28] == 0  # Window records only paint backgrounds.
    assert struct.pack('<I', key) in data
    exported = resolved.export(data)
    assert tomlkit.parse(exported).unwrap()['fonts'] == {
        'Handwriting': f'fonts/{key:08x}.cfont'
    }
    (tmp_path / 'fonts').mkdir()
    (tmp_path / 'fonts' / f'{key:08x}.cfont').write_bytes(bitmap)
    document.write_text(exported)
    assert cli.compile_inputs(command).setups['show'] == data


def test_asset_transfer_and_failed_load_preserve_active_fonts(
    peer: Path, bitmap: bytes, tmp_path: Path
) -> None:
    with NativePeer([str(peer), str(tmp_path)]) as port:
        key = crc32(bitmap)
        with transport.SerialConnection(Path(port)) as connection:
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
    font_data: dict[int, bytes] = {}
    data = compiler.compile_show(source, definitions, [], target, font_data)
    key, bitmap = next(iter(font_data.items()))
    size = next(iter(assets.entries(bitmap)))
    with NativePeer([str(peer), str(tmp_path)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
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


ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize('function', [False, True])
def test_unicode_labels_compile_transfer_and_export(
    peer: Path, tmp_path: Path, function: bool
) -> None:
    source = compiler.read_document(ROOT / 'firmware/test/display.toml')
    assert isinstance(source, dict)
    source['patch_cycles'] = {
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
        source['patch_cycles']['Été'].append(
            {
                'exit_buttons': 'A+F',
                'display_name': 'Été original',
                'operation': 'stock-firmware',
            }
        )
        target = target.model_copy(
            update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')}
        )
    font_data: dict[int, bytes] = {}
    data = compiler.compile_show(
        source,
        compiler.read_document(ROOT / 'firmware/test/definitions.json'),
        [],
        target,
        font_data,
    )
    assert 'Café e\u0301' in resolved.export(data)
    with NativePeer([str(peer), str(tmp_path)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            for k, b in font_data.items():
                client.copy_font(k, b)
            client.copy('Unicode', data, False)
            client.ok('CNTRL SETUP FINISH')
            assert client.download('Unicode') == data


def test_bundled_outline_fonts_use_sparse_unicode_assets() -> None:
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(
                name='target', paths=[ROOT / 'test/data/font-target.toml']
            ),
            False,
            [],
        )
    )
    show = models.Setup(
        instruments={}, patches={}, style=models.Style(font_name='CourierPrime')
    )
    font_data: dict[int, bytes] = {}
    _, _, block = display.compile_display(
        show,
        [
            display.DisplayPatch(
                values={'patch_cycle.name': 'Été', 'display_name': 'Café'}
            )
        ],
        target,
        font_data,
    )
    assert font_data and block[display.HEADER.size] == 255
    for k, b in font_data.items():
        face = assets.decode(b, 'CourierPrime', k)
        assert set().union(*(set(m.runs) for m in face.sizes.values())) == {
            'Été',
            'Café',
        }


@pytest.mark.parametrize(
    'family', ['StillTime', 'CaveatBold', 'classic', 'Arial', 'ComicSansMS']
)
def test_scaled_styles_fit_and_survive_export(family: str, tmp_path: Path) -> None:
    source = compiler.read_document(ROOT / 'firmware/test/show.json')
    assert isinstance(source, dict)
    source['fill_in_styles'] = False
    source['style'] = {'font_name': family, 'horizontal_scale': 1.25}
    source['patch_views'] = {
        'patch': {
            'fields': ['patch_cycle.name', 'display_name'],
            'line_styles': {'patch_cycle.name': {'horizontal_scale': 1.0}},
        }
    }
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(
                name='target', paths=[ROOT / 'test/data/font-target.toml']
            ),
            False,
            [],
        )
    )
    definitions = compiler.read_document(ROOT / 'firmware/test/definitions.json')
    font_data: dict[int, bytes] = {}
    data = compiler.compile_show(source, definitions, [], target, font_data)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert f'{family}@1.25x' in exported['fonts']
    if family in ('StillTime', 'CaveatBold'):
        assert family in exported['fonts']
    for key, bitmap in font_data.items():
        cli.save_asset(tmp_path / 'fonts' / f'{key:08x}.cfont', bitmap)
    path = tmp_path / 'exported.toml'
    path.write_text(resolved.export(data))
    command = cli.Sources(
        document=[str(path)], target=[str(ROOT / 'test/data/font-target.toml')]
    )
    assert (
        cli.compile_inputs(command).setups['exported']
        == setup_bundle.reindex({'exported': data})['exported']
    )


def test_still_time_default_scale_can_be_overridden() -> None:
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(
                name='target', paths=[ROOT / 'test/data/font-target.toml']
            ),
            False,
            [],
        )
    )
    for face in target.fonts:
        assert face.horizontal_scale == (1.5 if face.name == 'StillTime' else 1.0)
    source = models.Setup(
        fill_in_styles=False, style=models.Style(font_name='StillTime')
    )
    patch = display.DisplayPatch(
        values={'patch_cycle.name': 'world', 'display_name': 'BowedSaw'}
    )
    font_data: dict[int, bytes] = {}
    _, _, default = display.compile_display(source, [patch], target, font_data)
    assert b'StillTime@1.5x' in default
    source = source.model_copy(
        update={'style': models.Style(font_name='StillTime', horizontal_scale=1.0)}
    )
    _, _, unscaled = display.compile_display(source, [patch], target, font_data)
    assert b'StillTime@1.5x' not in unscaled
    assert default != unscaled
