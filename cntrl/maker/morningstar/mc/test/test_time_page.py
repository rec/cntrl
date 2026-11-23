"""Time uses the shared runtime for physical controls and MIDI clock output."""

from pathlib import Path

import pytest
from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_chooser import compiler, setup_bundle


@pytest.mark.parametrize('autoselect', [False, True])
def test_time_controls_measure_clear_select_and_relay(
    tmp_path: Path, file_regression: FileRegressionFixture, autoselect: bool
) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    source = {
        'pages': {'live': {'min_displays': 0}, 'time': {'type': 'time'}},
        'regions': {'performance': {'pages': ['live']}, 'tools': {'pages': ['time']}},
        'layout': {'focus': 'tools'},
        'page_settings': {'clock_outputs': ['din'], 'autoselect_clock': autoselect},
        'use_animations': False,
        'fill_in_styles': False,
    }
    raw = compiler.compile_show(source, {}, [], target)
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build({'show': raw}, {'show': source}, target.memory).pack()
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
    image = Image.new('RGB', (480, 384))
    with backend.Session(
        layout, target, executable, tmp_path / 'session', bundle
    ) as session:
        now = 0

        def advance(duration: int) -> None:
            nonlocal now
            now += duration
            session.advance(now)

        def press(button: str, duration: int = 60000) -> None:
            session.button(button, True)
            advance(duration)
            session.button(button, False)
            advance(60000)

        def snapshot(row: int) -> None:
            directory = tmp_path / f'view-{row}'
            cli.save(session, directory)
            for i in range(3):
                with Image.open(directory / f'lcd-{i + 1}.png') as panel:
                    image.paste(panel, (i * 160, row * 128))

        def ticks(endpoint: str) -> None:
            for _ in range(25):
                session.midi(endpoint, b'\xf8')
                advance(20000)

        advance(1000000)
        snapshot(0)
        blank = session.frames['lcd-3']
        ticks('din')
        assert not [m for m in session.messages if m.kind == 'midi']
        if not autoselect:
            press('e')
        press('b')
        session.messages.clear()
        session.midi('trs', b'\xf8')
        advance(1000)
        assert not [m for m in session.messages if m.kind == 'midi']
        session.midi('din', b'\xf8')
        advance(1000)
        assert [(m.endpoint, m.data) for m in session.messages if m.kind == 'midi'] == [
            ('din', b'\xf8')
        ]
        press('b')
        session.messages.clear()
        ticks('din')
        assert not [m for m in session.messages if m.kind == 'midi']
        # A successful tap selects Tap even when an input was selected.
        press('c')
        advance(380000)
        press('c')
        advance(100000)
        assert session.frames['lcd-3'] != blank
        snapshot(1)
        advance(1000000)
        # Input clocks expire, but Tap keeps its measured value.
        assert session.frames['lcd-3'] != blank
        press('c', 600000)
        advance(500000)
        snapshot(2)
        assert session.frames['lcd-3'] == blank
        assert not [m for m in session.messages if m.kind == 'error']
        tap_source = session.frames['lcd-2']
        press('e')
        din_source = session.frames['lcd-2']
        assert din_source != tap_source
        advance(1000000)
        assert session.frames['lcd-2'] == din_source
        press('e')
        assert session.frames['lcd-2'] not in (tap_source, din_source)
        press('e')
        assert session.frames['lcd-2'] == tap_source
    path = tmp_path / 'rendered-time-page.png'
    image.save(path)
    file_regression.check(
        path.read_bytes(), binary=True, extension='.png', basename='time-page'
    )


BASE = Path(__file__).parents[1]
