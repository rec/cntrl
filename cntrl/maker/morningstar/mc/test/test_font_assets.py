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
from cntrl.patch_switcher import cli, compiler, models, resolved, transport


@pytest.fixture(scope='module')
def bitmap() -> bytes:
    return font_assets.encode(
        ROOT / 'cntrl/fonts/courier-prime/CourierPrime-Regular.ttf'
    )


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
    assert data[28] == 255 and struct.unpack_from('<I', data, 40)[0] == key
    exported = resolved.export(data)
    assert tomlkit.parse(exported).unwrap()['fonts'] == {
        'Handwriting': f'fonts/{key:08x}.cfont'
    }
    (tmp_path / 'fonts').mkdir()
    (tmp_path / 'fonts' / f'{key:08x}.cfont').write_bytes(bitmap)
    document.write_text(exported)
    assert cli.compile_inputs(command)['show'] == data


def test_font_metrics_match_existing_converter(bitmap: bytes) -> None:
    key = crc32(bitmap)
    face = font_assets.decode(bitmap, 'Custom', key)
    target = models.Target.model_validate(
        compiler.read_document(MC / 'patch-switcher-target.json')
    )
    original = next(f for f in target.fonts if f.name == 'CourierPrime')
    assert face.sizes == original.sizes


@pytest.mark.parametrize('kind', ['truncated', 'index', 'descriptor', 'extra'])
def test_malformed_fonts_are_rejected(bitmap: bytes, kind: str) -> None:
    data = bytearray(bitmap)
    if kind == 'truncated':
        data = data[:-1]
    elif kind == 'index':
        data[31:50] = bytes([255] * 19)
    elif kind == 'descriptor':
        data[9] = 2
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
    data = compiler.compile_show(source, definitions, [], target)
    key = crc32(bitmap)
    size = data[29]
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


def test_compiler_rejects_fonts_that_exceed_the_loaded_ram_budget(
    bitmap: bytes, tmp_path: Path
) -> None:
    offset = font_assets.HEADER.size
    for _ in range(font_assets.HEADER.unpack_from(bitmap)[1]):
        fields = list(font_assets.ENTRY.unpack_from(bitmap, offset))
        ni, nd = fields[-2:]
        if fields[0] == 24:
            payload = bitmap[
                offset + font_assets.ENTRY.size : offset
                + font_assets.ENTRY.size
                + ni
                + nd
            ]
            fields[-1] = 90000 - ni
            padded = (
                font_assets.HEADER.pack(b'CFN1', 1)
                + font_assets.ENTRY.pack(*fields)
                + payload
                + bytes(90000 - len(payload))
            )
            break
        offset += font_assets.ENTRY.size + ni + nd
    else:
        pytest.fail('missing sample size')
    first, second = tmp_path / 'first.cfont', tmp_path / 'second.cfont'
    first.write_bytes(padded)
    second.write_bytes(padded[:-1] + b'\x01')
    source = compiler.read_document(ROOT / 'firmware/test/display.toml')
    assert isinstance(source, dict)
    source['fonts'] = {'First': str(first), 'Second': str(second)}
    source['style'] = {'font_name': 'First'}
    source['pages'] = {
        'patch': {
            'fields': ['group.name', 'display_name'],
            'line_styles': {'display_name': {'font_name': 'Second'}},
        }
    }
    source['windows'] = {}
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    with pytest.raises(compiler.CompilationError, match='96 KiB RAM budget'):
        compiler.compile_show(
            source,
            compiler.read_document(ROOT / 'firmware/test/definitions.json'),
            [],
            target,
        )


ROOT = Path(__file__).resolve().parents[5]
MC = Path(__file__).resolve().parents[1]
