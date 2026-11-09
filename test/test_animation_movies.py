"""Configured movie lists compile to portable SD assets, not resident pixel buffers."""

import struct
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit
from PIL import Image

from cntrl.animation import asset
from cntrl.patch_switcher import cli, compiler, models, resolved
from scripts import native_tests


@pytest.mark.parametrize(
    ('value', 'expected'),
    [
        ('', []),
        ([], []),
        ('a.gif', ['a.gif']),
        ('a.gif:b.webp', ['a.gif', 'b.webp']),
        (['a.gif', 'b.webp'], ['a.gif', 'b.webp']),
    ],
)
def test_movie_list_forms(value: object, expected: list[str]) -> None:
    setup = models.Setup.model_validate({'splash_movie': value, 'idle_movie': value})
    assert setup.splash_movie == expected
    assert setup.idle_movie == expected


@pytest.mark.parametrize(
    'value', [[''], 'a.gif:', ':a.gif', 'a.gif::b.gif', True, [1], None]
)
def test_movie_entries_must_be_nonempty_strings(value: object) -> None:
    with pytest.raises(ValueError):
        models.Setup.model_validate({'splash_movie': value})


@pytest.mark.parametrize('extension', ['gif', 'webp', 'png', 'jpg'])
def test_image_formats_prepare_oriented_frames(tmp_path: Path, extension: str) -> None:
    path = tmp_path / f'movie.{extension}'
    Image.new('RGB', (200, 160), 'red').save(path)
    data = asset.prepare_movie(path, GEOMETRY)
    info = asset.inspect_asset(data)
    assert info.frames == 1
    assert info.duration_ms == 1000
    assert struct.unpack_from('>H', data, asset.HEADER.size + 4)[0] >> 11 >= 30


@pytest.mark.parametrize('extension', ['gif', 'webp'])
def test_animation_preserves_frame_durations(tmp_path: Path, extension: str) -> None:
    path = tmp_path / f'movie.{extension}'
    image = Image.new('RGB', (160, 128), 'red')
    image.save(
        path,
        save_all=True,
        append_images=[Image.new('RGB', image.size, 'blue')],
        duration=[100, 250],
    )
    data = asset.prepare_movie(path, GEOMETRY)
    assert asset.inspect_asset(data).frames == 2
    assert struct.unpack_from('<I', data, asset.HEADER.size)[0] == 100
    assert (
        struct.unpack_from('<I', data, asset.HEADER.size + GEOMETRY.frame_bytes)[0]
        == 250
    )


def test_setup_assets_round_trip_without_reserving_movie_pixels_in_ram2(
    tmp_path: Path,
) -> None:
    # A movie larger than all of RAM2 still fits because its frames stay on SD.
    movie = asset.encode_frames(
        bytes(GEOMETRY.width * GEOMETRY.height * 2 * 8), [100] * 8, GEOMETRY
    )
    (tmp_path / 'source.cnsa').write_bytes(movie)
    source = tmp_path / 'setup.toml'
    source.write_text("splash_movie = 'source.cnsa'\nidle_movie = ['source.cnsa']\n")
    target = ROOT / 'firmware/test/target.json'
    geometry = tmp_path / 'animation.toml'
    geometry.write_text(tomlkit.dumps({'animation': GEOMETRY.model_dump()}))
    target_expression = [str(target), '+', str(geometry)]
    assets: dict[int, bytes] = {}
    bundle = cli.compile_inputs(
        cli.Sources(document=[str(source)], target=target_expression), movies=assets
    )
    key = crc32(movie)
    assert assets == {key: movie}
    assert len(movie) > 524288
    assert bundle.total_setup_ram < 4096
    assert bundle.prefill == [0]
    assert bundle.total_ram2_needs() == bundle.memory.runtime + 275
    exported = resolved.export(bundle.setups['setup'])
    settings = tomlkit.parse(exported).unwrap()
    assert (
        settings['splash_movie'] == settings['idle_movie'] == [f'movies/{key:08x}.cnsa']
    )
    cli.save_asset(tmp_path / f'movies/{key:08x}.cnsa', movie)
    source.write_text(exported)
    rebuilt = cli.compile_inputs(
        cli.Sources(document=[str(source)], target=target_expression)
    )
    assert rebuilt.setups == bundle.setups
    image = tmp_path / 'setup.bin'
    image.write_bytes(bundle.setups['setup'])
    executable = tmp_path / 'movie-setup'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{ROOT / "firmware/include"}',
            str(ROOT / 'firmware/test/animation/test_setup_movies.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    native_tests.run_native([str(executable), str(image), f'{key:08x}'], check=True)


def test_missing_and_unsupported_movies_fail_before_transfer(tmp_path: Path) -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    ).model_copy(update={'animation': GEOMETRY})
    path = tmp_path / 'missing.gif'
    with pytest.raises(compiler.CompilationError, match='does not exist'):
        compiler.compile_show({'splash_movie': str(path)}, {}, [], target)
    path.write_bytes(b'not an image')
    with pytest.raises(compiler.CompilationError, match='cannot identify image'):
        compiler.compile_show({'idle_movie': [str(path)]}, {}, [], target)


ROOT = Path(__file__).resolve().parents[1]

GEOMETRY = asset.Geometry(
    width=160, height=128, streams=[asset.Stream(), asset.Stream(mirror_x=True)]
)
