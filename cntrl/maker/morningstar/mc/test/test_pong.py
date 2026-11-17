"""Play Pong through the native emulator's real buttons and framebuffers."""

from pathlib import Path

import pytest
from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_switcher import compiler, setup_bundle


@pytest.mark.parametrize('default_catalogue', [False, True])
def test_pong_spans_screens_and_releases_navigation_controls(
    tmp_path: Path, file_regression: FileRegressionFixture, default_catalogue: bool
) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    source = (
        {'layout': {'focus': 'tools'}, 'fill_in_styles': False}
        if default_catalogue
        else compiler.read_document(BASE / 'emulator/pong.toml')
    )
    raw = compiler.compile_show(source, {}, [], target)
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build({'pong': raw}, {'pong': source}, target.memory).pack()
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
    with backend.Session(
        layout, target, executable, tmp_path / 'session', bundle
    ) as session:
        now = 0

        def advance(duration: int) -> None:
            nonlocal now
            now += duration
            session.advance(now)

        def press(button: str) -> None:
            session.button(button, True)
            advance(60000)
            session.button(button, False)
            advance(60000)

        def ball_on(screen: str) -> bool:
            frame = session.frames[screen]
            return any(frame[i : i + 2] == b'\xff\xe0' for i in range(0, len(frame), 2))

        advance(1000000)
        if default_catalogue:
            # The native adapter skips the hardware-only Panic page.
            for _ in range(9):
                press('f')
            advance(500000)
        press('f')  # Before serving, F must browse past Pong.
        advance(500000)
        assert not any(ball_on(s) for s in session.frames)
        session.button('d', True)
        advance(60000)
        press('f')  # D + F must browse backwards to the unstarted game.
        session.button('d', False)
        advance(500000)
        assert ball_on('lcd-2')
        cli.save(session, tmp_path / 'ready')
        image = Image.new('RGB', (480, 128))
        for i in range(3):
            with Image.open(tmp_path / 'ready' / f'lcd-{i + 1}.png') as panel:
                image.paste(panel, (i * 160, 0))
        image.save(tmp_path / 'pong.png')

        def compare_pixels(obtained: Path, expected: Path) -> None:
            with Image.open(obtained) as first, Image.open(expected) as second:
                assert first.size == second.size
                assert first.convert('RGB').tobytes() == second.convert('RGB').tobytes()

        file_regression.check(
            (tmp_path / 'pong.png').read_bytes(),
            binary=True,
            extension='.png',
            check_fn=compare_pixels,
            basename='test_pong_spans_screens_and_releases_navigation_controls',
        )
        assert ball_on('lcd-2')
        press('b')
        advance(500000)
        assert ball_on('lcd-3')
        advance(3000000)
        assert ball_on('lcd-1')
        press('b')  # Pause, then let outstanding display transfers settle.
        advance(200000)
        paused = dict(session.frames)
        advance(1000000)
        assert session.frames == paused
        session.button('d', True)
        session.button('f', True)  # F is a paddle control while Pong is focused.
        advance(200000)
        session.button('d', False)
        session.button('f', False)
        advance(200000)
        assert session.frames['lcd-1'] != paused['lcd-1']
        assert session.frames['lcd-3'] != paused['lcd-3']
        assert session.frames['lcd-2'] == paused['lcd-2']
        press('e')  # The default cycle exits to None, restoring all patch windows.
        advance(500000)
        assert not any(ball_on(s) for s in session.frames)
        for _ in range(10 if default_catalogue else 1):
            press('f')  # Cycle back to the paused game.
        advance(500000)
        assert ball_on('lcd-1')
        before = dict(session.frames)
        press('b')
        advance(200000)
        assert session.frames != before
        assert not [e.message for e in session.messages if e.kind == 'error']
        assert session.memory()['failures'] == 0


BASE = Path(__file__).parents[1]
