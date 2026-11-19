"""Catalogue profiles bind to executable targets without changing hardware facts."""

from pathlib import Path

import pytest

from cntrl.animation import asset
from cntrl.emulator import backend, cli
from cntrl.emulator.backend import bind_target
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator.profiles import adapt, export
from cntrl.patch_switcher import compiler, data_files, models, setup_bundle
from cntrl.patch_switcher.cli import compile_to_file


@pytest.fixture
def base() -> models.Target:
    target = Path(__file__).parents[1] / 'patch-switcher-target.json'
    errors: list[str] = []
    data = data_files.load_target(
        data_files.DataFile(name='target', paths=[target]), True, errors
    )
    assert not errors
    return models.Target.model_validate(data)


@pytest.mark.parametrize('controller,count', [('mc6_pro', 6), ('mc8_pro', 8)])
def test_profiles_place_every_declared_component(
    base: models.Target, controller: str, count: int
) -> None:
    layout = EmulatorLayout.load(
        Path(__file__).parents[1]
        / 'emulator'
        / (controller.replace('_', '-') + '.toml')
    )
    target = adapt(base, controller)
    hardware = bind_target(layout, target)
    assert len(hardware.controls) == len(target.buttons) == count
    assert len(hardware.displays) == len(target.displays) == count // 2
    assert target.max_switches == base.max_switches == 5
    assert target.controls.app_actions['a'] == base.controls.app_actions['A']
    assert target.functions == base.functions
    assert target.button_labels == {b.lower(): b for b in 'ABCDEFGH'[:count]}
    assert target.animation is not None and base.animation is not None
    assert len(target.animation.streams) == count // 2
    assert target.animation.width == base.animation.width
    assert target.animation.height == base.animation.height
    assert all(not s.mirror_x and not s.mirror_y for s in target.animation.streams)


def test_extra_switches_have_independent_page_directions(base: models.Target) -> None:
    target = adapt(base, 'mc8_pro')
    assert target.controls.previous_page == 'g'
    assert target.controls.next_page == 'h'
    assert target.controls.reverse_page is None
    assert target.controls.back == 'f'
    assert target.controls.app_actions['f'] == base.controls.app_actions['F']


def test_virtual_geometry_does_not_rewrite_catalogue_facts(base: models.Target) -> None:
    root = Path(__file__).parents[1] / 'emulator'
    current = EmulatorLayout.load(root / 'mc6-pro.toml')
    larger = EmulatorLayout.load(root / 'mc8-pro.toml')
    assert not current.provisional
    assert larger.provisional
    assert current.hardware().displays[0].width == 200
    assert larger.hardware().displays[0].width is None
    assert adapt(base, 'mc6_pro').displays[0].width == base.displays[0].width
    assert all(
        d.width == 160 and d.height == 128 for d in adapt(base, 'mc8_pro').displays
    )


def test_exported_profile_keeps_font_paths_relocatable(tmp_path: Path) -> None:
    source = Path(__file__).parents[1] / 'patch-switcher-target.json'
    path = export(source, 'mc8_pro', tmp_path / 'profile')
    path.parent.rename(tmp_path / 'moved-profile')
    path = tmp_path / 'moved-profile' / path.name
    errors: list[str] = []
    data = data_files.load_target(
        data_files.DataFile(name='target', paths=[path]), True, errors
    )
    assert not errors
    target = models.Target.model_validate(data)
    assert target.controls.previous_page == 'g'
    assert set(target.font_sources) == {f.name for f in target.fonts}
    assert all(
        Path(p).parent == path.parent / 'fonts' for p in target.font_sources.values()
    )
    assert all(Path(p).is_file() for p in target.font_sources.values())


def test_unknown_profile_is_rejected(base: models.Target) -> None:
    with pytest.raises(ValueError, match='no Morningstar emulator profile'):
        adapt(base, 'other')


@pytest.mark.parametrize('controller,count', [('mc6-pro', 6), ('mc8-pro', 8)])
def test_short_command_compiles_a_setup_and_preserves_session_data(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    controller: str,
    count: int,
    capsys: pytest.CaptureFixture[str],
) -> None:
    source = tmp_path / 'performance.toml'
    source.write_text(
        '[patch_cycles]\n'
        'volume = [{type="control-change", output="din", channel=1, '
        'controller=7, value=100}]\n'
    )
    monkeypatch.chdir(tmp_path)
    command = cli.Emulate(setup=source, controller=controller)
    layout, target, setup, storage = cli.prepare(command, compile_to_file)
    assert len(layout.controls) == len(target.buttons) == count
    assert storage == Path('.cntrl-emulator') / controller
    compiled = setup.read_bytes()
    assert list(setup_bundle.unpack(compiled)) == ['performance']
    assert all(Path(p).is_file() for p in target.font_sources.values())
    saved = storage / 'saved-pedal-state'
    saved.write_bytes(b'keep me')
    _, _, second, _ = cli.prepare(command, compile_to_file)
    assert second.read_bytes() == compiled
    assert saved.read_bytes() == b'keep me'
    assert source.read_text().startswith('[patch_cycles]')
    assert capsys.readouterr().out == ''


def test_custom_layout_accepts_a_compiled_bundle_and_explicit_storage(
    tmp_path: Path,
) -> None:
    root = Path(__file__).parents[1]
    target_path = export(None, 'mc6_pro', tmp_path / 'profile')
    target = cli.load_target(target_path)
    raw = compiler.compile_show({}, {}, [], target)
    bundle = tmp_path / 'existing.bin'
    bundle.write_bytes(
        setup_bundle.build({'existing': raw}, {'existing': {}}, target.memory).pack()
    )
    layout, actual, setup, storage = cli.prepare(
        cli.Emulate(
            setup=bundle,
            layout=root / 'emulator' / 'mc6-pro.toml',
            target=target_path,
            storage=tmp_path / 'saved-session',
        ),
        compile_to_file,
    )
    assert len(layout.controls) == len(actual.buttons) == 6
    assert setup == bundle
    assert storage == tmp_path / 'saved-session'
    assert not (storage / '.emulator-build' / 'setup').exists()


@pytest.mark.filterwarnings('error:foreground/background distance')
def test_real_setup_preparation_is_quiet(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    source = Path(__file__).parents[5] / 'simple-setup.toml'
    _, _, setup, _ = cli.prepare(
        cli.Emulate(setup=source, controller='mc6-pro', storage=tmp_path / 'session'),
        compile_to_file,
    )
    assert 'simple-setup' in setup_bundle.unpack(setup.read_bytes())
    captured = capsys.readouterr()
    assert captured.out == captured.err == ''


@pytest.mark.parametrize('controller', ['mc6_pro', 'mc8_pro'])
def test_exported_profiles_build_without_the_physical_sdk(
    tmp_path: Path, controller: str
) -> None:
    root = Path(__file__).parents[5]
    source = Path(__file__).parents[1] / 'patch-switcher-target.json'
    path = export(source, controller, tmp_path / 'profile')
    target = cli.load_target(path)
    executable = backend.build(target, tmp_path / 'native', root)
    show = {
        'fill_in_styles': False,
        'pages': {'performance': {'type': 'patch-switcher'}},
        'patches': {
            'control': {
                'patches': [
                    {
                        'type': 'control-change',
                        'output': 'din',
                        'channel': 1,
                        'controller': 7,
                        'value': 100,
                    },
                    {
                        'operation': 'keyboard',
                        'action': 'press',
                        'keys': [4],
                        'modifiers': 2,
                    },
                    {
                        'operation': 'keyboard',
                        'action': 'release',
                        'keys': [4],
                        'modifiers': 2,
                    },
                ],
            },
        },
        'patch_cycles': {'main': ['control']},
    }
    raw = compiler.compile_show(show, {}, [], target)
    setup = tmp_path / 'setup.bin'
    setup.write_bytes(
        setup_bundle.build({'emulator': raw}, {'emulator': show}, target.memory).pack()
    )
    layout = EmulatorLayout.load(
        Path(__file__).parents[1]
        / 'emulator'
        / (controller.replace('_', '-') + '.toml')
    )
    with backend.Session(
        layout, target, executable, tmp_path / 'session', setup
    ) as session:
        session.advance(20000)
        session.button('a', True)
        session.advance(40000)
        session.button('a', False)
        session.advance(60000)
        cli.save(session, tmp_path / 'actual-render')
        assert len(session.frames) == len(target.displays)
        assert any(
            e.kind == 'midi' and e.endpoint == 'din' and e.data == b'\xb0\x07\x64'
            for e in session.messages
        ), session.messages
        assert [e.data for e in session.messages if e.kind == 'keyboard'] == [
            bytes(8),  # Initial report after USB connection.
            b'\x02\0\x04\0\0\0\0\0',
            bytes(8),
        ]


def test_pedal_utility_pages_keep_their_controls_and_simulate_hardware(
    tmp_path: Path,
) -> None:
    root = Path(__file__).parents[5]
    target = cli.load_target(export(None, 'mc6_pro', tmp_path / 'profile'))
    source = {
        'pages': {
            'performance': {'type': 'patch-switcher', 'min_windows': 0},
            'status': {'type': 'status'},
            'panic': {'type': 'panic'},
        },
        'layout': {'focus': 'tools'},
        'patches': {'stock': {'operation': 'stock-firmware', 'exit_buttons': 'a+f'}},
        'patch_cycles': {'main': ['stock']},
        'page_settings': {
            'clock_outputs': ['din'],
        },
    }
    raw = compiler.compile_show(source, {}, [], target)
    bundle = tmp_path / 'utilities.bin'
    bundle.write_bytes(
        setup_bundle.build(
            {'utilities': raw}, {'utilities': source}, target.memory
        ).pack()
    )
    executable = backend.build(target, tmp_path / 'native', root)
    layout = EmulatorLayout.load(Path(__file__).parents[1] / 'emulator/mc6-pro.toml')
    with backend.Session(
        layout, target, executable, tmp_path / 'session', bundle
    ) as session:

        def press(button: str) -> None:
            session.button(button, True)
            session.advance(session.time_us + 60000)
            session.button(button, False)
            session.advance(session.time_us + 60000)

        session.advance(1000000)
        press('f')
        press('c')
        session.advance(session.time_us + 1000000)
        assert any(
            e.kind == 'midi' and e.endpoint == 'din' and e.data == b'\xb0\x7b\0'
            for e in session.messages
        ), session.messages
        press('f')
        assert not any(e.kind == 'firmware' for e in session.messages)
        press('a')  # The stock-firmware patch remains available without its old page.
        assert len([e for e in session.messages if e.kind == 'firmware']) == 1
        assert session.stock_firmware
        transition = next(
            i for i, e in enumerate(session.messages) if e.kind == 'firmware'
        )
        assert not any(
            e.kind in ('midi', 'keyboard', 'relay')
            for e in session.messages[transition + 1 :]
        )
        before = dict(session.frames)
        press('f')
        session.advance(session.time_us + 1000000)
        assert session.frames == before
        assert len([e for e in session.messages if e.kind == 'firmware']) == 1
        assert not any(
            e.kind in ('midi', 'keyboard', 'relay')
            for e in session.messages[transition + 1 :]
        )
        assert not [e.message for e in session.messages if e.kind == 'error']


def test_mirrored_movies_preserve_physical_stream_geometry(tmp_path: Path) -> None:
    target = cli.load_target(export(None, 'mc6_pro', tmp_path / 'profile', mirror=True))
    assert target.animation_display_streams == [0, 1, 0]
    geometry = target.animation
    assert geometry is not None and len(geometry.streams) == 2
    red, blue = b'\xf8\0', b'\0\x1f'
    row = red * (geometry.width // 2) + blue * (geometry.width // 2)
    pixels = row * geometry.height
    movie = tmp_path / 'asymmetric.cnsa'
    movie.write_bytes(asset.encode_frames(pixels, [1000], geometry))
    source = {'splash_movie': [str(movie)]}
    movies: dict[int, bytes] = {}
    raw = compiler.compile_show(source, {}, [], target, movie_assets=movies)
    bundle = tmp_path / 'movies.bin'
    bundle.write_bytes(
        setup_bundle.build({'movies': raw}, {'movies': source}, target.memory).pack()
    )
    storage = tmp_path / 'session'
    folder = storage / 'cntrl/movies'
    folder.mkdir(parents=True)
    for key, data in movies.items():
        (folder / f'{key:08x}.cnsa').write_bytes(data)
    executable = backend.build(target, tmp_path / 'native', Path(__file__).parents[5])
    layout = EmulatorLayout.load(Path(__file__).parents[1] / 'emulator/mc6-pro.toml')
    with backend.Session(layout, target, executable, storage, bundle) as session:
        session.advance(250000)
        assert session.frames['lcd-1'] == session.frames['lcd-3'] == pixels
        assert (
            session.frames['lcd-2']
            == (blue * (geometry.width // 2) + red * (geometry.width // 2))
            * geometry.height
        )
        assert not [e.message for e in session.messages if e.kind == 'error']
