"""Play Pong through the native emulator's real buttons and framebuffers."""

from pathlib import Path

import pytest
from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_chooser import compiler, compiler_inputs, models, setup_bundle


@pytest.mark.parametrize('default_catalogue', [False, True])
def test_pong_spans_screens_and_releases_navigation_controls(
    tmp_path: Path, file_regression: FileRegressionFixture, default_catalogue: bool
) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    source = (
        {'layout': {'focus': 'tools'}, 'fill_in_styles': False}
        if default_catalogue
        else compiler_inputs.read_document(BASE / 'emulator/pong.toml')
    )
    assert isinstance(source, dict)
    utility_pages = [
        p.type
        for p in models.Setup.model_validate(source).pages.values()
        if p.type != 'patch-chooser'
    ]
    source['page_settings'] = {'status_font': 'TamzenBold7x13'}
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

        def press(button: str, duration: int = 60000) -> None:
            session.button(button, True)
            advance(duration)
            session.button(button, False)
            advance(60000)

        def ball_on(screen: str) -> bool:
            frame = session.frames[screen]
            return any(frame[i : i + 2] == b'\xff\xe0' for i in range(0, len(frame), 2))

        advance(1000000)
        if default_catalogue:
            # Every compiled utility page, including Panic, stays available.
            for _ in range(utility_pages.index('pong')):
                press('f', 600000)
            advance(500000)
        assert ball_on('lcd-2')
        # Exit is available before the first serve, as advertised by the legend.
        press('e')
        advance(500000)
        assert not any(ball_on(s) for s in session.frames)
        for _ in range(len(utility_pages) - 1 if default_catalogue else 1):
            press('f', 600000)
        advance(500000)
        assert ball_on('lcd-2')
        press('f', 600000)  # Before serving, holding F browses past Pong.
        advance(500000)
        assert not any(ball_on(s) for s in session.frames)
        press('f')
        press('f', 600000)  # Click and hold returns to the unstarted game.
        advance(500000)
        assert ball_on('lcd-2')
        middle = session.frames['lcd-2']
        assert (
            sum(
                middle[(y * 160 + 80) * 2 : (y * 160 + 80) * 2 + 2] == b'\x84\x10'
                for y in range(26, 113)
            )
            >= 60
        )
        assert any(
            middle[(y * 160 + x) * 2 : (y * 160 + x) * 2 + 2] == b'\xff\xff'
            for y in range(115, 126)
            for x in range(160)
        )
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
        press('e')  # The default cycle advances to the Language page.
        advance(500000)
        assert not any(ball_on(s) for s in session.frames)
        for _ in range(len(utility_pages) - 1 if default_catalogue else 1):
            press('f', 600000)  # Cycle back to the paused game.
        advance(500000)
        assert ball_on('lcd-1')
        before = dict(session.frames)
        press('b')
        advance(200000)
        assert session.frames != before
        assert not [e.message for e in session.messages if e.kind == 'error']
        assert session.memory()['failures'] == 0


BASE = Path(__file__).parents[1]
