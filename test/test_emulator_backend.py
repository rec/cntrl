"""Native controller scenarios use the production compiler, renderer and MIDI."""

import struct
from collections.abc import Callable
from hashlib import sha256
from io import BytesIO
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
from cntrl.fonts import assets, catalogue, ui
from cntrl.patch_chooser import (
    compiler,
    compiler_inputs,
    device_globals,
    languages,
    models,
    setup_bundle,
)
from cntrl.patch_chooser.cli import compile_to_file
from cntrl.patch_chooser.global_overlay import Overlay


class BackendFiles(BaseModel, frozen=True):
    layout: EmulatorLayout
    target: models.Target
    executable: Path
    setup: Path


@pytest.mark.parametrize('mirrored', [False, True])
@pytest.mark.parametrize('installed', [False, True])
def test_native_language_status_tracks_boot_and_pedal_selection(
    backend_files: BackendFiles, tmp_path: Path, mirrored: bool, installed: bool
) -> None:
    files = backend_files
    target = files.target.model_copy(
        update={
            'controls': models.TargetControls(confirm='a', next='b'),
        }
    )
    setup = tmp_path / 'language.bin'
    language_page = len(target.displays) > 1
    write_bundle(
        {
            'language': {
                'pages': {'tool': {'type': 'language' if language_page else 'status'}},
            }
        },
        target,
        setup,
    )
    executable = backend.build(
        target, files.setup.parent / 'language-native', Path(__file__).parents[1]
    )
    global_folder = tmp_path / 'cntrl/global'
    global_folder.mkdir(parents=True)
    (global_folder / '0.bin').write_bytes(
        device_globals.journal(Overlay(language='fr'))
    )
    if installed:
        folder = tmp_path / 'cntrl/languages'
        folder.mkdir()
        (folder / 'fr.bin').write_bytes(languages.pack('fr'))
    if mirrored:
        (tmp_path / 'mirror.toml').write_text('')
        (tmp_path / 'status-snapshot.bin').write_bytes(b'CSS1' + bytes(107))
    with backend.Session(files.layout, target, executable, tmp_path, setup) as session:
        assert session.language == ('fr' if installed else 'en')
        if installed and language_page:
            session.button('b', True)
            session.advance(session.time_us + 60000)
            session.button('b', False)
            session.advance(session.time_us + 60000)
            session.button('a', True)
            session.advance(session.time_us + 60000)
            session.button('a', False)
            session.advance(1_000_000)
            assert session.language == 'en'
    assert sorted(p.name for p in (tmp_path / 'cntrl/languages').glob('*.bin')) == (
        ['fr.bin'] if installed else []
    )


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
        'pages': {'performance': {'type': 'patch-chooser'}},
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
    data = compiler_inputs.read_document(root / 'firmware/test/target.json')
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
            'Arimo',
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


def test_calibrate_page_saves_each_expression_jack(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    jacks = [f'configurable-{i}' for i in range(1, 4)]
    target = files.target.model_copy(
        update={
            'controls': models.TargetControls(confirm='a', previous='b'),
            'trs_ports': {j: ['expression'] for j in jacks},
        }
    )
    setup = tmp_path / 'expression.bin'
    write_bundle(
        {
            'expression': {
                'pages': {'calibrate': {'type': 'calibrate'}},
                'trs_ports': dict.fromkeys(jacks, 'expression'),
            }
        },
        target,
        setup,
    )
    executable = backend.build(
        target, files.setup.parent / 'calibration-native', Path(__file__).parents[1]
    )
    storage = tmp_path / 'session'
    calibration = storage / 'cntrl/calibration.toml'
    calibration.parent.mkdir(parents=True)
    calibration.write_text('[configurable-1]\nname = "Boss EV-30, left"\n')
    with backend.Session(files.layout, target, executable, storage, setup) as session:

        def press(button: str) -> None:
            session.button(button, True)
            session.advance(session.time_us + 60000)
            session.button(button, False)
            session.advance(session.time_us + 60000)

        def move(jack: int, start: int, end: int) -> None:
            for step in range(1, 9):
                session.expression(jack, start + (end - start) * step // 8)
                session.advance(session.time_us + 10000)

        for jack in range(3):
            session.expression(jack, 512)
        session.advance(1000000)
        press('a')
        ranges = [(100, 900), (920, 120), (200, 820)]
        for jack in (2, 0, 1):
            heel, toe = ranges[jack]
            move(jack, 512, heel)
            move(jack, heel, toe)
        session.advance(session.time_us + 2100000)
        press('a')
    saved = tomlkit.parse(calibration.read_text()).unwrap()
    assert saved['configurable-1']['name'] == 'Boss EV-30, left'
    assert saved['bits'] == 12
    for jack, (heel, toe) in enumerate(ranges, 1):
        assert saved[f'configurable-{jack}']['heel'] == round(heel * 4095 / 1023)
        assert saved[f'configurable-{jack}']['toe'] == round(toe * 4095 / 1023)


def test_mirrored_status_uses_captured_values_and_remains_frozen(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    setup = tmp_path / 'status.bin'
    write_bundle(
        {'status': {'pages': {'status': {'type': 'status'}}}}, files.target, setup
    )
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path / 'preview', setup
    ) as session:
        preview = dict(session.frames)
    storage = tmp_path / 'mirror'
    storage.mkdir()
    (storage / 'mirror.toml').write_text('')
    snapshot = bytearray(b'CSS1' + bytes(107))
    struct.pack_into('<QQI', snapshot, 36, 118, 123456789, 0xB7C7EB4)
    snapshot[65] = 1
    (storage / 'status-snapshot.bin').write_bytes(snapshot)
    with backend.Session(
        files.layout, files.target, files.executable, storage, setup
    ) as session:
        captured = dict(session.frames)
        assert session.mode == 'pedal mirror'
        assert captured != preview
        session.advance(3600000000)
        assert session.frames == captured


def test_cable_test_cycles_runs_locally_and_releases_midi_on_exit(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    target = files.target.model_copy(
        update={'controls': models.TargetControls(tap='a', next_page='b')}
    )
    setup = tmp_path / 'cable.bin'
    write_bundle(
        {
            'cable': {
                'pages': {
                    'status': {'type': 'status'},
                    'cable': {'type': 'cable-test'},
                },
                'midi': {'thru': {'from': 'din', 'to': 'trs'}},
            }
        },
        target,
        setup,
    )
    executable = backend.build(target, tmp_path / 'native', Path(__file__).parents[1])
    with backend.Session(files.layout, target, executable, tmp_path, setup) as session:

        def press(button: str) -> None:
            session.button(button, True)
            session.advance(session.time_us + 60000)
            session.button(button, False)
            session.advance(session.time_us + 60000)

        press('b')
        session.advance(session.time_us + 500000)
        idle = dict(session.frames)
        press('a')
        active = dict(session.frames)
        assert active != idle
        assert not any(e.kind == 'midi' for e in session.messages)
        # Return the complete probe cycle, including the most recent values.
        session.midi('din', b''.join(bytes([0xB0, 2, v]) for v in range(128)))
        session.advance(session.time_us + 80000)
        assert session.frames != active
        assert not any(e.kind == 'midi' for e in session.messages)
        press('b')
        session.messages.clear()
        session.midi('din', bytes([0x90, 60, 100]))
        assert any(
            e.kind == 'midi' and e.endpoint == 'trs' and e.data == b'\x90\x3c\x64'
            for e in session.messages
        )


def test_status_font_samples_cycle_without_navigating_or_repeating_held_f(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    setup = tmp_path / 'comparison.bin'
    write_bundle(
        {'comparison': {'pages': {'status': {'type': 'status'}}}}, files.target, setup
    )
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, setup
    ) as session:
        if 'f' not in files.target.buttons:
            with pytest.raises(ValueError, match='switch labelled F'):
                session.compare_status_fonts()
            return
        session.compare_status_fonts()
        assert all(s.capital_height == 7 for s in session.font_samples)
        assert any(
            b'\xff\xff' in session.frames[d.id][: d.width * 20 * 2]
            for d in session.target.displays
        )
        first = dict(session.frames)
        session.button('f', True)
        second = dict(session.frames)
        assert second != first
        session.button('f', True)
        assert session.frames == second
        session.button('f', False)
        for _ in range(len(session.font_samples) - 1):
            session.button('f', True)
            session.button('f', False)
        assert session.font_sample_index == 0
        assert session.frames == first
        assert not any(e.kind == 'error' for e in session.messages)


def test_fixed_size_utility_font_renders_monitor_body(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    sources = dict(files.target.font_sources)
    sources['TamzenBold7x13'] = str(
        Path(__file__).parents[1] / 'cntrl/fonts/bitmap_samples/TamzenBold7x13.cfont'
    )
    target = files.target.model_copy(update={'font_sources': sources})
    setup = tmp_path / 'monitor.bin'
    write_bundle(
        {'monitor': {'pages': {'monitor': {'type': 'midi-monitor'}}}}, target, setup
    )
    with backend.Session(
        files.layout, target, files.executable, tmp_path, setup
    ) as session:
        assert any(
            sum(
                session.frames[d.id][(y * d.width + x) * 2 : (y * d.width + x) * 2 + 2]
                == b'\x00\x00'
                for y in range(22, 110)
                for x in range(4, d.width - 4)
            )
            > 20
            for d in target.displays
        )
        assert not any(e.kind == 'error' for e in session.messages)


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


def test_recovered_globals_initialize_only_fresh_emulator_storage(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    setup = tmp_path / 'setup.bin'
    setup.write_bytes(backend_files.setup.read_bytes())
    target = tmp_path / 'target.toml'
    target.write_text(
        tomlkit.dumps(backend_files.target.model_dump(mode='json', exclude_none=True))
    )
    overlay = tmp_path / 'device-globals.toml'
    overlay.write_bytes(device_globals.dump(Overlay(values={0: 80})))
    storage = tmp_path / 'storage'
    command = cli.Emulate(
        setup=setup,
        target=target,
        layout=backend_files.setup.parent / 'layout.toml',
        storage=storage,
    )
    cli.prepare(command, compile_to_file)
    journal = storage / 'cntrl/global/0.bin'
    assert journal.read_bytes() == device_globals.journal(Overlay(values={0: 80}))
    overlay.write_bytes(device_globals.dump(Overlay(values={0: 160})))
    cli.prepare(command, compile_to_file)
    assert journal.read_bytes() == device_globals.journal(Overlay(values={0: 80}))


def test_font_replacement_changes_rendering_without_recompiling(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    original = backend_files.target
    executable = backend_files.executable
    timestamp = executable.stat().st_mtime_ns
    with backend.Session(
        backend_files.layout,
        original,
        executable,
        tmp_path / 'before',
        backend_files.setup,
    ) as session:
        session.advance(20000)
        before = dict(session.frames)
    font = tmp_path / 'replacement.cfont'
    font.write_bytes(
        ui.prepare(
            'Arimo',
            [8, 12, 16, 20, 24, 32, 48, 64, 96],
            catalogue.bundled_sources()['AtkinsonNextSemiBold'],
        )
    )
    target = original.model_copy(
        update={'font_sources': {n: str(font) for n in original.ui_fonts}}
    )
    assert (
        backend.build(target, executable.parent, Path(__file__).parents[1])
        == executable
    )
    assert executable.stat().st_mtime_ns == timestamp
    with backend.Session(
        backend_files.layout,
        target,
        executable,
        tmp_path / 'after',
        backend_files.setup,
    ) as session:
        session.advance(20000)
        assert (
            session.frames[original.displays[0].id] != before[original.displays[0].id]
        )


def test_new_patch_font_loads_without_recompiling(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    target = backend_files.target
    show = control_setup(100)
    show.update(
        fill_in_styles=False,
        fonts={'AddedFont': str(catalogue.bundled_sources()['AtkinsonNextSemiBold'])},
        style={'font_name': 'AddedFont'},
    )
    fonts: dict[int, bytes] = {}
    raw = compiler.compile_show(show, {}, [], target, assets=fonts)
    assert fonts
    setup = tmp_path / 'setup.bin'
    setup.write_bytes(
        setup_bundle.build(
            {'added-font': raw}, {'added-font': show}, target.memory
        ).pack()
    )
    storage = tmp_path / 'session'
    directory = storage / 'cntrl/fonts'
    directory.mkdir(parents=True)
    for key, data in fonts.items():
        (directory / f'{key:08x}.cfont').write_bytes(data)
    executable = backend_files.executable
    timestamp = executable.stat().st_mtime_ns
    assert (
        backend.build(target, executable.parent, Path(__file__).parents[1])
        == executable
    )
    assert executable.stat().st_mtime_ns == timestamp
    with backend.Session(
        backend_files.layout, target, executable, storage, setup
    ) as session:
        session.advance(20000)
        assert any(session.frames[target.displays[0].id])


@pytest.mark.parametrize('problem', ['missing', 'header', 'pixels', 'trailing'])
def test_unreadable_runtime_font_reports_the_file(
    backend_files: BackendFiles, tmp_path: Path, problem: str
) -> None:
    font = tmp_path / 'runtime-font.cfont'
    if problem == 'header':
        font.write_bytes(b'invalid font')
    elif problem != 'missing':
        data = bytearray(
            Path(next(iter(backend_files.target.font_sources.values()))).read_bytes()
        )
        if problem == 'pixels':
            data[8 + assets.ENTRY.size + 12] ^= 1
        else:
            data.extend(b'trailing bytes')
        font.write_bytes(data)
    target = backend_files.target.model_copy(
        update={'font_sources': {n: str(font) for n in backend_files.target.ui_fonts}}
    )
    with pytest.raises(ValueError, match='UI font.*runtime-font'):
        backend.Session(
            backend_files.layout,
            target,
            backend_files.executable,
            tmp_path / 'session',
            backend_files.setup,
        )


def test_preview_storage_rejects_a_second_setup_until_the_session_ends(
    backend_files: BackendFiles,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    files = backend_files
    target_path = tmp_path / 'target.toml'
    target_path.write_text(
        tomlkit.dumps(files.target.model_dump(mode='json', exclude_none=True))
    )
    second = tmp_path / 'second.bin'
    write_bundle({'emulator': control_setup(45)}, files.target, second)
    scenario = tmp_path / 'scenario.toml'
    scenario.write_text('until = 0.01\n')
    command = cli.Emulate(
        setup=files.setup,
        layout=files.setup.with_name('layout.toml'),
        target=target_path,
        storage=tmp_path / 'shared',
        scenario=scenario,
    )
    competing = command.model_copy(update={'setup': second})
    original_build = cli.build
    original_run = Scenario.run

    def reject_competing_setup() -> None:
        with pytest.raises(ValueError, match='another emulator session is in progress'):
            cli.emulate(competing, compile_to_file)

    def build(target: models.Target, directory: Path, source_root: Path) -> Path:
        reject_competing_setup()
        return original_build(target, directory, source_root)

    def run(replay: Scenario, session: backend.Session) -> None:
        reject_competing_setup()
        original_run(replay, session)

    with monkeypatch.context() as patch:
        patch.setattr(cli, 'build', build)
        patch.setattr(Scenario, 'run', run)
        cli.emulate(command, compile_to_file)
    cli.emulate(competing, compile_to_file)
    assert command.storage is not None
    assert (command.storage / 'setup-source.sha256').read_text() == sha256(
        second.read_bytes()
    ).hexdigest()


def test_replay_stops_on_native_rejection_and_session_can_continue(
    backend_files: BackendFiles, tmp_path: Path
) -> None:
    files = backend_files
    replay = Scenario.model_validate(
        {
            'until': 0.1,
            'events': [
                {'at': 0, 'kind': 'connection', 'target': 'din', 'value': 0},
                {'at': 0.01, 'kind': 'midi', 'target': 'din', 'data': [0x90, 60, 100]},
                {'at': 0.02, 'kind': 'connection', 'target': 'din', 'value': 1},
            ],
        }
    )
    scenario = tmp_path / 'replay.toml'
    scenario.write_text(tomlkit.dumps(replay.model_dump(mode='json')))
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        with pytest.raises(backend.RequestRejected, match='MIDI input is disconnected'):
            Scenario.load(scenario).run(session)
        assert session.time_us == 10000
        assert session.events == [replay.events[0]]
        session.connection('din', True)
        session.midi('din', bytes([0x90, 60, 100]))
        assert any(e.kind == 'midi' and e.endpoint == 'trs' for e in session.messages)


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


def test_display_response_composes_rectangles_in_order(
    backend_files: BackendFiles, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    files = backend_files
    display = files.target.displays[0]
    y = display.height - 2
    replies = [
        struct.pack('<B5H', 1, 0, 2, y, 3, 2) + struct.pack('>6H', 1, 2, 3, 4, 5, 6),
        struct.pack('<B5H', 1, 0, 3, y, 1, 2) + struct.pack('>2H', 7, 8),
        b'\0',
    ]
    wire = BytesIO(b''.join(struct.pack('<I', len(r)) + r for r in replies))
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        before = dict(session.frames)
        expected = bytearray(before[display.id])
        first = 2 * (y * display.width + 2)
        second = first + 2 * display.width
        expected[first : first + 6] = struct.pack('>3H', 1, 7, 5)
        expected[second : second + 6] = struct.pack('>3H', 2, 8, 6)
        with monkeypatch.context() as patch:
            patch.setattr(session, 'read', wire.read)
            session.receive()
        assert session.frames == {**before, display.id: bytes(expected)}


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


def test_interrupted_startup_reaps_the_native_session(
    backend_files: BackendFiles,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    spawned: list[backend.Session] = []

    def interrupt(session: backend.Session) -> None:
        spawned.append(session)
        raise KeyboardInterrupt

    monkeypatch.setattr(backend.Session, 'receive', interrupt)
    files = backend_files
    with pytest.raises(KeyboardInterrupt):
        backend.Session(
            files.layout, files.target, files.executable, tmp_path, files.setup
        )
    (session,) = spawned
    assert session.process.returncode is not None
    assert session.process.stdin is not None and session.process.stdin.closed
    assert session.process.stdout is not None and session.process.stdout.closed
    assert not session.reader.is_alive()


def test_a_pipe_close_failure_still_reaps_the_process_and_reader(
    backend_files: BackendFiles,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    files = backend_files
    session = backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    )
    pipe = session.process.stdin
    assert pipe is not None

    def fail() -> None:
        raise OSError('injected pipe close failure')

    try:
        with monkeypatch.context() as patch:
            patch.setattr(pipe, 'close', fail)
            with pytest.raises(OSError, match='injected pipe close failure'):
                session.close()
        assert session.process.returncode is not None
        assert session.process.stdout is not None and session.process.stdout.closed
        assert not session.reader.is_alive()
    finally:
        session.close()
    assert pipe.closed


@pytest.mark.parametrize('partial', [False, True])
@pytest.mark.parametrize('replay', [False, True])
def test_an_unresponsive_native_request_has_a_deadline(
    backend_files: BackendFiles,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    partial: bool,
    replay: bool,
) -> None:
    files = backend_files
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path, files.setup
    ) as session:
        session.response_timeout = 0.1

        class Clock:
            now = 0.0

            def monotonic(self) -> float:
                return self.now

        clock = Clock()

        class Watchdog:
            def __init__(self, interval: float, action: Callable[[], object]) -> None:
                self.deadline = clock.now + interval
                self.action = action
                self.started = self.cancelled = self.fired = False
                self.daemon = False

            def start(self) -> None:
                self.started = True

            def cancel(self) -> None:
                self.cancelled = True

        watchdogs: list[Watchdog] = []

        def timer(interval: float, action: Callable[[], object]) -> Watchdog:
            watchdog = Watchdog(interval, action)
            watchdogs.append(watchdog)
            return watchdog

        def get(timeout: float) -> bytes:
            clock.now += timeout / 2 if partial and clock.now == 0 else timeout
            watchdog = watchdogs[0]
            if (
                watchdog.started
                and not watchdog.cancelled
                and clock.now >= watchdog.deadline
            ):
                watchdog.action()  # Kill the real native process at its deadline.
                watchdog.fired = True
            if partial:
                return b'\0'  # Incomplete frames must not restart the deadline.
            raise backend.Empty

        monkeypatch.setattr(backend, 'time', clock)
        monkeypatch.setattr(backend, 'Timer', timer)
        monkeypatch.setattr(session.output, 'get', get)
        with pytest.raises(ValueError, match='native emulator response timed out'):
            session.advance(0xFFFFFFFFFFFFFFFF, replay=replay)
        assert clock.now == pytest.approx(session.response_timeout)
        assert watchdogs[0].started and watchdogs[0].fired and watchdogs[0].cancelled
        assert session.failed
        with pytest.raises(ValueError, match='no longer usable'):
            session.request(b'\0')
    assert session.process.returncode is not None
    assert not session.reader.is_alive()


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


@pytest.mark.parametrize('changed_source', [False, True])
def test_reopening_storage_applies_only_changed_source(
    backend_files: BackendFiles, tmp_path: Path, changed_source: bool
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
    # Simulate saved edits by retaining the installed replacement while restoring
    # the record of the original supplied source.
    (storage / 'setup-source.sha256').write_text(
        sha256(files.setup.read_bytes()).hexdigest()
    )
    unrelated = storage / 'notes.txt'
    unrelated.write_text('retain this session file')
    if changed_source:
        write_bundle({'emulator': control_setup(102)}, files.target, replacement)
    with backend.Session(
        files.layout,
        files.target,
        files.executable,
        storage,
        replacement if changed_source else files.setup,
    ) as session:
        session.advance(20000)
        session.button('a', True)
        session.advance(40000)
        session.button('a', False)
        session.advance(60000)
        assert any(
            e.kind == 'midi'
            and e.endpoint == 'din'
            and e.data == bytes((0xB0, 7, 102 if changed_source else 101))
            for e in session.messages
        ), session.messages
        assert not any(
            e.kind == 'midi' and e.data == b'\xb0\x07\x64' for e in session.messages
        )
    assert unrelated.read_text() == 'retain this session file'


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
        assert bytes(session.events[0].data) == message
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


@pytest.mark.parametrize('segment_us', [10_000_000, 7_000])
def test_recorded_inputs_replay_the_same_midi_and_pixels(
    backend_files: BackendFiles,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    segment_us: int,
) -> None:
    files = backend_files
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path / 'original', files.setup
    ) as session:
        session.advance(20001)
        session.midi('din', b'\x90\x3c')
        session.button('a', True)
        session.midi('trs', b'\xf8')
        session.midi('din', b'\x64')
        session.advance(40001)
        session.button('a', False)
        session.advance(80000)
        expected_messages = list(session.messages)
        expected_frames = dict(session.frames)
        path = tmp_path / 'recording.toml'
        session.write_replay(path)
    scenario = Scenario.load(path)
    assert [(e.at, e.kind, e.target, bytes(e.data)) for e in scenario.events] == [
        (0.020001, 'midi', 'din', b'\x90\x3c'),
        (0.020001, 'button', 'a', b''),
        (0.020001, 'midi', 'trs', b'\xf8'),
        (0.020001, 'midi', 'din', b'\x64'),
        (0.040001, 'button', 'a', b''),
    ]
    monkeypatch.setattr(backend, 'MAX_REPLAY_ADVANCE_US', segment_us)
    with backend.Session(
        files.layout, files.target, files.executable, tmp_path / 'replayed', files.setup
    ) as session:
        scenario.run(session)
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
