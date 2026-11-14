"""Native controller scenarios use the production compiler, renderer and MIDI."""

from pathlib import Path
from time import monotonic

import pytest
import tomlkit
from PIL import Image
from pydantic import BaseModel

from cntrl.animation import asset
from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.emulator.replay import Scenario
from cntrl.fonts import catalogue, ui
from cntrl.patch_switcher import compiler, models, setup_bundle


class BackendFiles(BaseModel, frozen=True):
    layout: EmulatorLayout
    target: models.Target
    executable: Path
    setup: Path


def control_setup(value: int) -> dict[str, object]:
    return {
        'patches': {
            'control': {
                'type': 'control-change',
                'output': 'din',
                'channel': 1,
                'controller': 7,
                'value': value,
            }
        },
        'patch_cycles': {'main': ['control']},
        'pages': {'performance': {'type': 'patch-switcher'}},
    }


def write_bundle(
    sources: dict[str, object], target: models.Target, path: Path
) -> dict[int, bytes]:
    movies: dict[int, bytes] = {}
    compiled = {
        n: compiler.compile_show(
            s, {}, [], target, documents=sources, movie_assets=movies
        )
        for n, s in sources.items()
    }
    path.write_bytes(
        setup_bundle.build(
            setup_bundle.reindex(compiled), sources, target.memory
        ).pack()
    )
    return movies


@pytest.fixture(scope='module', params=[1, 4])
def backend_files(
    tmp_path_factory: pytest.TempPathFactory, request: pytest.FixtureRequest
) -> BackendFiles:
    root = Path(__file__).parents[1]
    directory = tmp_path_factory.mktemp('emulator-backend')
    count = request.param
    assert isinstance(count, int)
    buttons = 'AB' if count == 1 else 'ABCDEFGH'
    (directory / 'catalogue.toml').write_text(
        f"[example]\nname='Example controller'\nbuttons='{buttons}'\n"
        + 'displays=['
        + ','.join("'96x128'" for _ in range(count))
        + "]\n[example.ports]\nusb='usb_c'\n"
    )
    (directory / 'layout.toml').write_text(
        "catalogue='catalogue.toml'\ncontroller='example'\nwidth=100\nheight=60\n"
        + '[controls]\n'
        + '\n'.join(
            f'{b.lower()}={{x={5 + i * 11},y=40,width=10,height=10}}'
            for i, b in enumerate(buttons)
        )
        + '\n[displays]\n'
        + '\n'.join(
            f'lcd-{i + 1}={{x={5 + i * 24},y=5,width=20,height=30}}'
            for i in range(count)
        )
        + '\n'
    )
    data = compiler.read_document(root / 'firmware/test/target.json')
    assert isinstance(data, dict)
    data['buttons'] = list(buttons.lower())
    data['controls'] = {'app_actions': {'a': 'activate-selection', 'b': 'next-patch'}}
    data['displays'] = [
        {'id': f'lcd-{i + 1}', 'label': f'Display {i + 1}', 'width': 96, 'height': 128}
        for i in range(count)
    ]
    data['animation'] = {
        'width': 96,
        'height': 128,
        'streams': [{} for _ in range(count)],
    }
    data['functions'] = []
    data['relay_outputs'] = ['relay']
    # A synthetic target uses one real outline for every test face. Production
    # rasterization still decodes the ordinary exported CFN2 format.
    font = directory / 'font.cfont'
    font.write_bytes(
        ui.prepare(
            'classic',
            [8, 12, 16, 20, 24, 32, 48, 64, 96],
            catalogue.bundled_sources()['CourierPrime'],
        )
    )
    data['font_sources'] = {n: str(font) for n in data['ui_fonts']}
    target = models.Target.model_validate(data)
    show = control_setup(100)
    show.update(
        {
            'midi': {'thru': {'from': 'din', 'to': 'trs'}},
            'relays': {'relay': False},
        }
    )
    setup = directory / 'setup.bin'
    write_bundle({'emulator': show}, target, setup)
    return BackendFiles(
        layout=EmulatorLayout.load(directory / 'layout.toml'),
        target=target,
        executable=backend.build(target, directory / 'native', root),
        setup=setup,
    )


def test_native_build_reuses_the_compiled_executable(
    backend_files: BackendFiles,
) -> None:
    executable = backend_files.executable
    timestamp = executable.stat().st_mtime_ns
    started = monotonic()
    result = backend.build(
        backend_files.target, executable.parent, Path(__file__).parents[1]
    )
    elapsed = monotonic() - started
    assert result == executable
    assert executable.stat().st_mtime_ns == timestamp
    print(f'unchanged native build: {elapsed:.3f} s')


def test_replay_drives_real_button_actions_and_draws_pages(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    scenario = Scenario.model_validate(
        {
            'until': '80 ms',
            'events': [
                {'at': '20 ms', 'kind': 'button', 'target': 'a', 'value': 1},
                {'at': '40 ms', 'kind': 'button', 'target': 'a', 'value': 0},
            ],
        }
    )
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        scenario.run(session)
        cli.save(session, tmp_path / 'actual-render')
        assert any(
            e.kind == 'midi' and e.endpoint == 'din' and e.data == b'\xb0\x07\x64'
            for e in session.messages
        ), session.messages
        assert any(session.frames['lcd-1'])
        assert not [e.message for e in session.messages if e.kind == 'error']
        assert session.time_us == 80000


def test_blocked_output_delivers_only_after_acceptance(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        session.block('trs', True)
        session.cleanup()
        session.midi('din', b'\xb0\x4a\x37')
        session.advance(10000)
        assert not any(
            e.kind == 'midi' and e.endpoint == 'trs' and e.data == b'\xb0\x4a\x37'
            for e in session.messages
        )
        session.block('trs', False)
        session.advance(20000)
        assert any(
            e.kind == 'midi' and e.endpoint == 'trs' and e.data == b'\xb0\x4a\x37'
            for e in session.messages
        )


def test_screenshots_preserve_rgb565_primary_colours(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path / 'session', files.setup
    ) as session:
        display = files.target.displays[0]
        session.frames['lcd-1'] = b'\xf8\0\x07\xe0\0\x1f' + bytes(
            display.width * display.height * 2 - 6
        )
        cli.save(session, tmp_path / 'screenshots')
    with Image.open(tmp_path / 'screenshots/lcd-1.png') as image:
        assert image.mode == 'RGB'
        assert image.tobytes()[:9] == b'\xff\0\0\0\xff\0\0\0\xff'


def test_invalid_scenario_targets_do_not_apply_earlier_events(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    scenario = Scenario.model_validate(
        {
            'until': '40 ms',
            'events': [
                {'at': '10 ms', 'kind': 'button', 'target': 'a', 'value': 1},
                {'at': '20 ms', 'kind': 'midi', 'target': 'missing', 'data': [0xF8]},
            ],
        }
    )
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        with pytest.raises(ValueError, match='unknown MIDI endpoint'):
            scenario.run(session)
        assert session.time_us == 0 and not session.held
        assert not any(e.kind == 'midi' for e in session.messages)


def test_cleanup_releases_accepted_notes(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        session.midi('din', b'\x90\x3c\x64')
        session.advance(10000)
        assert any(
            e.kind == 'midi' and e.endpoint == 'trs' and e.data == b'\x90\x3c\x64'
            for e in session.messages
        )
        session.cleanup()
        assert any(
            e.kind == 'midi' and e.endpoint == 'trs' and e.data == b'\x80\x3c\0'
            for e in session.messages
        )


def test_an_unresponsive_native_request_has_a_deadline(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        session.response_timeout = 0.1
        started = monotonic()
        with pytest.raises(ValueError, match='native emulator'):
            session.advance(0xFFFFFFFFFFFFFFFF)
        assert monotonic() - started < 5


def test_bundle_references_execute_the_other_setup(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    local = control_setup(100)
    local['patches'] = {}
    local['patch_cycles'] = {'main': [{'document': 'other', 'patch': 'control'}]}
    bundle = tmp_path / 'references.bin'
    write_bundle({'emulator': local, 'other': control_setup(101)}, files.target, bundle)
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path / 'session', bundle
    ) as session:
        session.advance(20000)
        session.button('a', True)
        session.advance(40000)
        session.button('a', False)
        session.advance(60000)
        assert any(
            e.kind == 'midi' and e.endpoint == 'din' and e.data == b'\xb0\x07\x65'
            for e in session.messages
        ), session.messages


def test_reopening_storage_preserves_the_installed_bundle(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    storage = tmp_path / 'session'
    with backend.Session(
        files.layout, files.target, files.executable, storage, files.setup
    ) as session:
        session.advance(20000)
    replacement = tmp_path / 'replacement.bin'
    write_bundle({'emulator': control_setup(101)}, files.target, replacement)
    with backend.Session(
        files.layout, files.target, files.executable, storage, replacement
    ) as session:
        session.advance(20000)
        session.button('a', True)
        session.advance(40000)
        session.button('a', False)
        session.advance(60000)
        assert any(
            e.kind == 'midi' and e.endpoint == 'din' and e.data == b'\xb0\x07\x64'
            for e in session.messages
        ), session.messages
        assert not any(
            e.kind == 'midi' and e.data == b'\xb0\x07\x65' for e in session.messages
        )


def test_startup_animation_uses_the_shared_asset_player(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    geometry = files.target.animation
    assert geometry is not None
    red = b'\xf8\0' * (geometry.width * geometry.height)
    source = tmp_path / 'red.cnsa'
    source.write_bytes(asset.encode_frames(red, [1000], geometry))
    show = control_setup(100)
    show['splash_movie'] = [str(source)]
    bundle = tmp_path / 'animation.bin'
    movies = write_bundle({'emulator': show}, files.target, bundle)
    storage = tmp_path / 'session'
    assets = storage / 'cntrl' / 'movies'
    assets.mkdir(parents=True)
    for k, v in movies.items():
        (assets / f'{k:08x}.cnsa').write_bytes(v)
    with backend.Session(
        files.layout, files.target, files.executable, storage, bundle
    ) as session:
        session.advance(250000)
        assert all(f == red for f in session.frames.values()), session.messages
        session.button('a', True)
        session.advance(270000)
        session.button('a', False)
        session.advance(1500000)
        assert any(f != red for f in session.frames.values())


def test_large_sysex_keeps_its_message_across_transport_chunks(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    message = b'\xf0\x7d' + bytes([42]) * 70000 + b'\xf7'
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        session.midi('din', message)
        session.advance(10000)
        captured = b''.join(
            e.data for e in session.messages if e.kind == 'midi' and e.endpoint == 'trs'
        )
        assert captured == message


def test_long_preparation_finishes_before_the_request_is_acknowledged(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    show = control_setup(100)
    show['midi'] = {
        'thru': {
            'from': 'din',
            'to': 'trs',
            'via': [{'transpose': 0}] * 1100,
        }
    }
    bundle = tmp_path / 'preparation.bin'
    write_bundle({'emulator': show}, files.target, bundle)
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path / 'session', bundle
    ) as session:
        session.midi('din', b'\x90\x3c\x64')
        assert any(
            e.kind == 'midi' and e.endpoint == 'trs' and e.data == b'\x90\x3c\x64'
            for e in session.messages
        )


def test_recorded_inputs_replay_the_same_midi_and_pixels(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path / 'original', files.setup
    ) as session:
        session.advance(20000)
        session.button('a', True)
        session.advance(40000)
        session.button('a', False)
        session.advance(80000)
        scenario = Scenario(
            seed=session.random_seed,
            until=session.time_us / 1000000,
            events=session.events,
        )
        expected_messages = list(session.messages)
        expected_frames = dict(session.frames)
        path = tmp_path / 'recording.toml'
        path.write_text(tomlkit.dumps(scenario.model_dump(mode='json')))
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path / 'replayed', files.setup
    ) as session:
        Scenario.load(path).run(session)
        assert session.messages == expected_messages
        assert session.frames == expected_frames


def test_cleanup_preserves_the_clock_and_allows_further_input(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        session.advance(20000)
        session.cleanup()
        session.advance(21000)
        session.button('a', True)
        session.advance(41000)
        assert not [e.message for e in session.messages if e.kind == 'error']
        assert any(e.kind == 'midi' and e.endpoint == 'din' for e in session.messages)
