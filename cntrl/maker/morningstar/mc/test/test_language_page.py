"""The language page uses real controls, persisted preferences and coloured flags."""

from pathlib import Path

import pytest
from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.localization import catalogue
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_chooser import compiler, languages, models, setup_bundle


@pytest.mark.parametrize(
    ('frame_buffers', 'default_catalogue'),
    [(0, False), (1, False), (2, False), (1, True)],
)
def test_language_page_scrolls_selects_and_keeps_the_language_after_restart(
    tmp_path: Path,
    file_regression: FileRegressionFixture,
    frame_buffers: int,
    default_catalogue: bool,
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
    if default_catalogue:
        del source['pages']
        del source['regions']
        del source['layout']
    utility_pages = [
        p.type
        for p in models.Setup.model_validate(source).pages.values()
        if p.type != 'patch-chooser'
    ]
    language_position = utility_pages.index('language')
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
    image = Image.new('RGB', (480, 512))
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

        def marker_rows(frame: bytes) -> list[int]:
            pixels = [
                divmod(i // 2, 160)
                for i in range(0, len(frame), 2)
                if frame[i : i + 2] == b'\x05\xa0'
            ]
            assert sorted({x for _, x in pixels}) == list(range(5, 15))
            return sorted({y for y, _ in pixels})

        advance(1000000)
        for page in utility_pages[1 : language_position + 1]:
            press('f')
            assert session.tooltip_context[0] == page
        if language_position:
            advance(500000)
        english = session.frames['lcd-3']
        performance = session.frames['lcd-1']
        first = session.frames['lcd-2']
        colors = {first[i : i + 2] for i in range(0, len(first), 2)}
        assert {b'\xf8\0', b'\0\x18', b'\xff\xe0'} <= colors
        assert marker_rows(first) == list(range(59, 69))
        snapshot(0)
        # Verify a frame between English and Español, while the band stays put.
        session.button('b', True)
        advance(60000)
        moving = session.frames['lcd-2']
        session.button('b', False)
        advance(500000)
        settled = session.frames['lcd-2']
        assert moving != first and moving != settled
        assert marker_rows(moving) != marker_rows(first)
        for frame in (first, moving, settled):
            column = [
                frame[(y * 160 + 150) * 2 : (y * 160 + 151) * 2] for y in range(128)
            ]
            assert [i for i, p in enumerate(column) if p == b'\xfd\xc0'] == list(
                range(54, 74)
            )
        press('b')
        advance(500000)
        assert session.frames['lcd-2'] != first
        assert session.frames['lcd-1'] == performance
        assert session.frames['lcd-3'] == english
        assert marker_rows(session.frames['lcd-2']) == list(range(19, 29))
        session.capture_corrections(True)
        caption = next(r for r in session.correction_regions if r.message == 'language')
        session.preview_correction(caption, 'Language selection')
        press('c')
        assert session.requested_correction_language == 'fr'
        assert session.language == 'en'
        session.resolve_correction_language(False)
        assert session.requested_correction_language == ''
        assert session.language == 'en'
        assert marker_rows(session.frames['lcd-2']) == list(range(19, 29))
        press('c')
        session.clear_corrections()
        session.resolve_correction_language(True)
        advance(500000)
        assert session.language == 'fr'
        assert marker_rows(session.frames['lcd-2']) == list(range(59, 69))
        assert session.correction_language_name == 'Français'
        assert (
            next(
                r for r in session.correction_regions if r.message == 'language'
            ).original
            == 'Langue'
        )
        session.capture_corrections(False)
        french = session.frames['lcd-3']
        assert french != english
        snapshot(1)
        # Moving the candidate never changes the active language until C.
        # Walk back past Čeština and verify that the list clamps at both ends.
        for _ in range(6):
            press('e')
        advance(500000)
        assert session.frames['lcd-3'] == french
        snapshot(2)
        top = session.frames['lcd-2']
        press('e')
        assert session.frames['lcd-2'] == top
        for _ in range(len(catalogue.locales())):
            press('b')
        advance(500000)
        bottom = session.frames['lcd-2']
        assert bottom != top
        press('b')
        assert session.frames['lcd-2'] == bottom
        snapshot(3)
        assert not [m for m in session.messages if m.kind == 'error']
    path = tmp_path / 'rendered-language-page.png'
    image.save(path)
    file_regression.check(
        path.read_bytes(), binary=True, extension='.png', basename='language-page'
    )
    with backend.Session(layout, target, executable, storage, bundle) as session:
        now = 0
        advance(1000000)
        for _ in range(language_position):
            press('f')
        if language_position:
            advance(500000)
        assert session.frames['lcd-3'] == french
        press('f')
        advance(500000)
        assert session.frames['lcd-3'] != french
        center = session.frames['lcd-2']
        assert b'\xf8\0' not in {center[i : i + 2] for i in range(0, len(center), 2)}


BASE = Path(__file__).parents[1]
