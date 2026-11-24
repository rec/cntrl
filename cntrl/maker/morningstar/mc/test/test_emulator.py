"""Catalogue profiles bind to executable targets without changing hardware facts."""

import os
from pathlib import Path

import pytest
import tomlkit
from PIL import Image

from cntrl.animation import asset
from cntrl.emulator import backend, cli
from cntrl.emulator.backend import bind_target
from cntrl.emulator.layout import EmulatorLayout
from cntrl.emulator.replay import Scenario
from cntrl.maker.morningstar.mc.emulator.profiles import adapt, export
from cntrl.patch_chooser import (
    compiler,
    data_files,
    device_globals,
    languages,
    models,
    resolved,
    setup_bundle,
)
from cntrl.patch_chooser.cli import compile_to_file
from cntrl.patch_chooser.global_overlay import Overlay


@pytest.fixture
def base() -> models.Target:
    target = Path(__file__).parents[1] / 'patch-chooser-target.json'
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
    assert target.controls.settings_shortcut == (controller == 'mc6_pro')
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
    source = Path(__file__).parents[1] / 'patch-chooser-target.json'
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


@pytest.mark.parametrize('controller,count', [(None, 6), ('mc8-pro', 8)])
def test_short_command_compiles_a_setup_and_preserves_session_data(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    controller: str | None,
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
    build = tmp_path / 'cntrl/maker/morningstar/mc/doom/build'
    build.mkdir(parents=True)
    host = build / ('doom-host.exe' if os.name == 'nt' else 'doom-host')
    host.touch()
    (build / 'e1m1.img').touch()
    command = cli.Emulate(setup=source, controller=controller)
    layout, target, setup, storage = cli.prepare(command, compile_to_file)
    assert len(layout.controls) == len(target.buttons) == count
    assert storage == Path('.cntrl-emulator') / (controller or 'mc6-pro')
    compiled = setup.read_bytes()
    assert list(setup_bundle.unpack(compiled)) == ['performance']
    exported = tomlkit.parse(
        resolved.export(setup_bundle.unpack(compiled)['performance'])
    )
    rotation = exported['regions']['tools']['pages']
    if controller is None:
        assert command.playable_doom_host == host
        assert rotation[-4:] == ['pong', 'doom', 'memon', 'None']
    else:
        assert not {'doom', 'memon'} & set(rotation)
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


def test_real_setup_patch_chooser_remains_operable_beside_tools(
    tmp_path: Path,
) -> None:
    root = Path(__file__).parents[5]
    layout, target, setup, storage = cli.prepare(
        cli.Emulate(
            setup=root / 'simple-setup.toml',
            controller='mc6-pro',
            storage=tmp_path / 'session',
        ),
        compile_to_file,
    )
    executable = backend.build(target, tmp_path / 'native', root)
    with backend.Session(layout, target, executable, storage, setup) as session:

        def press(button: str) -> None:
            session.button(button, True)
            session.advance(session.time_us + 60000)
            session.button(button, False)
            session.advance(session.time_us + 60000)

        session.advance(1000000)
        session.messages.clear()
        press('a')
        press('b')
        press('a')
        press('e')
        press('a')
        assert [(m.endpoint, m.data) for m in session.messages if m.kind == 'midi'] == [
            ('din', b'\xc0\x05'),
            ('din', b'\xc0\x06'),
            ('din', b'\xc0\x0a'),
        ]


@pytest.mark.parametrize('controller', ['mc6_pro', 'mc8_pro'])
def test_exported_profiles_build_without_the_physical_sdk(
    tmp_path: Path, controller: str
) -> None:
    root = Path(__file__).parents[5]
    source = Path(__file__).parents[1] / 'patch-chooser-target.json'
    path = export(source, controller, tmp_path / 'profile')
    target = cli.load_target(path)
    executable = backend.build(target, tmp_path / 'native', root)
    show = {
        'fill_in_styles': False,
        'pages': {'performance': {'type': 'patch-chooser'}},
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


@pytest.mark.parametrize('styled', [False, True])
def test_status_switch_c_toggles_the_saved_owner_contact(
    tmp_path: Path, styled: bool
) -> None:
    target = cli.load_target(export(None, 'mc6_pro', tmp_path / 'profile'))
    layout = EmulatorLayout.load(Path(__file__).parents[1] / 'emulator/mc6-pro.toml')
    source: dict[str, object] = {
        'pages': {'status': {'type': 'status'}},
        'use_animations': False,
    }
    if styled:
        image = tmp_path / 'contact.png'
        Image.new('RGB', (160, 104), '#000080').save(image)
        style = {'image': str(image), 'foreground_color': 'black', 'font_name': 'Arimo'}
        source['page_settings'] = {'contact_style': style}
    setup = tmp_path / 'status.bin'
    raw = compiler.compile_show(source, {}, [], target)
    if styled:
        exported = tomlkit.parse(resolved.export(raw)).unwrap()
        assert exported['page_settings']['contact_style'] == style
        assert compiler.compile_show(exported, {}, [], target) == raw
        _, artwork = resolved.artwork_files(raw)
        assert len(artwork) == 1
    setup.write_bytes(
        setup_bundle.build({'status': raw}, {'status': source}, target.memory).pack()
    )
    storage = tmp_path / 'storage'
    journal = storage / 'cntrl/global/0.bin'
    journal.parent.mkdir(parents=True)
    journal.write_bytes(
        device_globals.journal(
            Overlay(name_and_contact='Alex Example\nowner@example.org')
        )
    )
    executable = backend.build(target, tmp_path / 'native', Path(__file__).parents[5])

    def press_c(session: backend.Session) -> None:
        session.button('c', True)
        session.advance(session.time_us + 60_000)
        session.button('c', False)
        session.advance(session.time_us + 80_000)

    with backend.Session(layout, target, executable, storage, setup) as session:
        session.advance(150_000)
        diagnostics = dict(session.frames)
        session.capture_corrections(True)
        press_c(session)
        multiline = dict(session.frames)
        assert multiline != diagnostics
        assert [
            r.original
            for r in session.correction_regions
            if r.message == 'owner_contact'
        ] == ['Contact']
        if styled:
            frame = multiline['lcd-1']
            body = frame[30 * 160 * 2 : 53 * 160 * 2]
            colors = {body[i : i + 2] for i in range(0, len(body), 2)}
            assert {b'\0\x10', b'\xff\xff'} <= colors
        assert not [m for m in session.messages if m.kind in ('midi', 'error')]
        press_c(session)
        assert session.frames == diagnostics

    journal.write_bytes(
        device_globals.journal(Overlay(name_and_contact='Alex Example'))
    )
    with backend.Session(layout, target, executable, storage, setup) as session:
        press_c(session)
        assert session.frames != multiline


def test_starter_status_labels_remain_editable_with_the_real_time_clock(
    tmp_path: Path,
) -> None:
    root = Path(__file__).parents[5]
    command = cli.Emulate(
        setup=Path(__file__).parents[1] / 'starter.toml',
        controller='mc6-pro',
        storage=tmp_path / 'starter-session',
    )
    layout, target, setup, storage = cli.prepare(command, compile_to_file)
    executable = backend.build(target, tmp_path / 'native', root)
    with backend.Session(layout, target, executable, storage, setup) as session:
        session.advance(4_000_000, replay=True)
        session.capture_corrections(True)
        for time_us in range(4_010_000, 4_060_000, 10_000):
            session.advance(time_us, replay=True)
            heading = next(
                r for r in session.correction_regions if r.message == 'status'
            )
            assert heading.screen == 0
        regions = session.correction_regions
        summary = [r for r in regions if r.message == 'cycles_patches']
        assert [r.editor_text(r.original) for r in summary] == ['Cycles:', 'Patches:']
        assert summary[0].right < summary[1].left
        assert [len(r.glyphs) for r in summary] == [7, 8]
        assert {
            'clock_value',
            'tap_value',
            'ram_value',
            'git_value',
            'starts_value',
            'uptime_value',
        } <= {r.message for r in regions}
        original = session.frames.copy()
        session.preview_correction(heading, 'Device status')
        assert session.frames['lcd-1'] != original['lcd-1']
        cycles = summary[0]
        session.preview_correction(
            cycles, cycles.replace_literal(cycles.original, 'Loops:')
        )
        updated = [
            r for r in session.correction_regions if r.message == 'cycles_patches'
        ]
        assert [len(r.glyphs) for r in updated] == [6, 8]


def test_pedal_utility_pages_keep_their_controls_and_simulate_hardware(
    tmp_path: Path,
) -> None:
    root = Path(__file__).parents[5]
    target = cli.load_target(export(None, 'mc6_pro', tmp_path / 'profile'))
    source = {
        'pages': {
            'performance': {'type': 'patch-chooser', 'min_displays': 0},
            'status': {'type': 'status'},
            'panic': {'type': 'panic'},
            'settings': {'type': 'settings'},
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
    language_dir = tmp_path / 'session/cntrl/languages'
    language_dir.mkdir(parents=True)
    (language_dir / 'fr.bin').write_bytes(languages.pack('fr'))
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
        session.capture_corrections(True)
        panic = next(r for r in session.correction_regions if r.message == 'panic')
        assert panic.original == 'Panic'
        original = session.frames.copy()
        session.preview_correction(panic, 'Alarm')
        assert session.frames != original
        assert (
            next(r for r in session.correction_regions if r.message == 'panic').original
            == 'Panic'
        )
        session.clear_corrections()
        assert session.frames == original
        session.capture_corrections(False)
        press('c')
        session.advance(session.time_us + 1000000)
        assert any(
            e.kind == 'midi' and e.endpoint == 'din' and e.data == b'\xb0\x7b\0'
            for e in session.messages
        ), session.messages
        before_settings = len(session.messages)
        press('f')  # Settings utility page.
        press('c')
        for button in ('a', 'c', 'b', 'b', 'b', 'c', 'b', 'c', 'b', 'c'):
            press(button)
        events = session.messages[before_settings:]
        assert [e.message for e in events] == [
            'Simulated boot default saved: stock firmware.'
        ]
        assert not session.stock_firmware
        assert session.language == 'en'
        for button in ('b', 'c', 'b', 'c'):  # Draft French and review.
            press(button)
        assert session.language == 'en'
        press('b')
        press('c')
        session.advance(session.time_us + 1000000)
        assert session.language == 'fr'
        press('f')
        press('f')
        press('f')
        before_switch = len(session.messages)
        press('a')  # The stock-firmware patch remains available without its old page.
        assert (
            len([e for e in session.messages[before_switch:] if e.kind == 'firmware'])
            == 1
        )
        assert session.stock_firmware
        transition = next(
            i
            for i, e in enumerate(session.messages)
            if i >= before_switch and e.kind == 'firmware'
        )
        assert not any(
            e.kind in ('midi', 'keyboard', 'relay')
            for e in session.messages[transition + 1 :]
        )
        before = dict(session.frames)
        press('f')
        session.advance(session.time_us + 1000000)
        assert session.frames == before
        assert (
            len([e for e in session.messages[before_switch:] if e.kind == 'firmware'])
            == 1
        )
        assert not any(
            e.kind in ('midi', 'keyboard', 'relay')
            for e in session.messages[transition + 1 :]
        )
        assert not [e.message for e in session.messages if e.kind == 'error']


def test_computer_keyboard_edits_song_and_setlist_names(tmp_path: Path) -> None:
    root = Path(__file__).parents[5]
    target = cli.load_target(export(None, 'mc6_pro', tmp_path / 'profile'))
    source = {
        'fill_in_styles': False,
        'pages': {
            'performance': {'type': 'patch-chooser', 'min_displays': 0},
            'status': {'type': 'status'},
            'songs': {'type': 'songs-and-setlists'},
        },
        'layout': {'focus': 'tools'},
        'patch_cycles': {
            'Sound': [
                {'type': 'program-change', 'output': 'din', 'channel': 1, 'program': 1}
            ]
        },
    }
    raw = compiler.compile_show(source, {}, [], target)
    bundle = tmp_path / 'names.bin'
    bundle.write_bytes(
        setup_bundle.build({'names': raw}, {'names': source}, target.memory).pack()
    )
    executable = backend.build(target, tmp_path / 'native', root)
    layout = EmulatorLayout.load(Path(__file__).parents[1] / 'emulator/mc6-pro.toml')
    storage = tmp_path / 'session'
    with backend.Session(layout, target, executable, storage, bundle) as session:
        session.record_replay(True)

        def press(button: str) -> None:
            session.button(button, True)
            session.advance(session.time_us + 60000)
            session.button(button, False)
            session.advance(session.time_us + 60000)

        def rename(name: str) -> dict[str, bytes]:
            cli.save(session, tmp_path / name)
            assert session.editing_name
            for _ in range(63):
                press('e')
            for _ in range(63):
                session.key(8)
            for c in name + 'X':
                session.key(ord(c))
            session.key(8)
            for _ in name:
                press('d')
            expected = dict(session.frames)
            session.key(10)
            assert not session.editing_name
            session.advance(session.time_us + 1000000)
            return expected

        assert not session.editing_name
        session.key(ord('x'))  # Typing outside Rename is ignored by the shared UI.
        session.advance(1000000)
        press('f')  # Status to Songs and setlists in the right-hand region.
        session.advance(session.time_us + 500000)
        launcher = session.frames['lcd-3']
        press('c')  # Open the song library tools directly.
        press('f')  # Back restores the utility page.
        session.advance(session.time_us + 500000)
        assert session.frames['lcd-3'] == launcher
        press('f')  # Page cycling remains available.
        assert session.frames['lcd-3'] != launcher
        press('f')
        assert session.frames['lcd-3'] == launcher
        press('c')  # Songs and setlists.
        for _ in range(3):
            press('b')
        press('c')  # Save current patch as a song, then browse the library.
        press('c')
        press('c')  # Rename song.
        expected = rename('abc!')
        press('c')
        cli.save(session, tmp_path / 'saved-song')
        assert session.editing_name and session.frames == expected
        saved = {
            p.name: p.read_bytes() for p in (storage / 'cntrl/songs').glob('*.bin')
        }
        session.key(ord('Z'))
        session.key(27)
        assert not session.editing_name
        assert {
            p.name: p.read_bytes() for p in (storage / 'cntrl/songs').glob('*.bin')
        } == saved
        press('c')
        assert session.editing_name and session.frames == expected
        session.key(27)
        press('f')  # Root menu.
        press('b')
        press('c')  # Setlists.
        press('c')  # New setlist.
        press('f')  # Performance.
        press('d')  # Edit setlist.
        for _ in range(4):
            press('b')
        press('c')  # Rename setlist.
        expected = rename('Friday')
        for _ in range(4):
            press('b')
        press('c')
        assert session.editing_name and session.frames == expected
        assert not [e.message for e in session.messages if e.kind == 'error']

        scenario = Scenario(
            seed=session.random_seed,
            until=session.time_us / 1000000,
            events=session.events,
        )
        replay_file = tmp_path / 'names.toml'
        replay_file.write_text(tomlkit.dumps(scenario.model_dump(mode='json')))
        expected_library = {
            p.name: p.read_bytes() for p in (storage / 'cntrl/songs').glob('*.bin')
        }
        assert expected_library

    replay_storage = tmp_path / 'replay'
    with backend.Session(layout, target, executable, replay_storage, bundle) as session:
        Scenario.load(replay_file).run(session)
        assert session.editing_name and session.frames == expected
        assert not [e.message for e in session.messages if e.kind == 'error']
        assert {
            p.name: p.read_bytes()
            for p in (replay_storage / 'cntrl/songs').glob('*.bin')
        } == expected_library


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
