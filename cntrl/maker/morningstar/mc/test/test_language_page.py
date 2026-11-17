"""The language page uses real controls, persisted preferences and coloured flags."""

from pathlib import Path

import pytest
from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.localization import catalogue
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_switcher import compiler, languages, setup_bundle


@pytest.mark.parametrize('frame_buffers', [0, 1, 2])
def test_language_page_scrolls_selects_and_keeps_the_language_after_restart(
    tmp_path: Path, file_regression: FileRegressionFixture, frame_buffers: int
) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    source = {
        'pages': {
            'live': {},
            'language': {'type': 'language'},
            'empty': {'type': 'None'},
        },
        'regions': {
            'performance': {'pages': ['live']},
            'tools': {'pages': ['language', 'empty']},
        },
        'layout': {'focus': 'tools'},
        'frame_buffers': frame_buffers,
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
    storage = tmp_path / 'session'
    folder = storage / 'cntrl/languages'
    folder.mkdir(parents=True)
    for tag in catalogue.locales():
        (folder / f'{tag}.bin').write_bytes(languages.pack(tag))
    image = Image.new('RGB', (480, 384))
    french = b''
    with backend.Session(layout, target, executable, storage, bundle) as session:
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

        def snapshot(row: int) -> None:
            directory = tmp_path / f'view-{row}'
            cli.save(session, directory)
            for i in range(3):
                with Image.open(directory / f'lcd-{i + 1}.png') as panel:
                    image.paste(panel, (i * 160, row * 128))

        advance(1000000)
        english = session.frames['lcd-3']
        performance = session.frames['lcd-1']
        first = session.frames['lcd-2']
        colors = {first[i : i + 2] for i in range(0, len(first), 2)}
        assert {b'\xf8\0', b'\0\x18', b'\xff\xe0'} <= colors
        snapshot(0)
        for _ in range(catalogue.locales().index('fr')):
            press('e')
        advance(500000)
        assert session.frames['lcd-2'] != first
        assert session.frames['lcd-1'] == performance
        assert session.frames['lcd-3'] == english
        press('c')
        advance(500000)
        french = session.frames['lcd-3']
        assert french != english
        snapshot(1)
        # Moving the candidate never changes the active language until C.
        for _ in range(catalogue.locales().index('fr') + 1):
            press('b')
        advance(500000)
        assert session.frames['lcd-3'] == french
        snapshot(2)
        assert not [m for m in session.messages if m.kind == 'error']
    path = tmp_path / 'rendered-language-page.png'
    image.save(path)
    file_regression.check(
        path.read_bytes(), binary=True, extension='.png', basename='language-page'
    )
    with backend.Session(layout, target, executable, storage, bundle) as session:
        session.advance(1000000)
        assert session.frames['lcd-3'] == french
        session.button('f', True)
        session.advance(1060000)
        session.button('f', False)
        session.advance(1600000)
        assert session.frames['lcd-3'] != french
        center = session.frames['lcd-2']
        assert b'\xf8\0' not in {center[i : i + 2] for i in range(0, len(center), 2)}


BASE = Path(__file__).parents[1]
