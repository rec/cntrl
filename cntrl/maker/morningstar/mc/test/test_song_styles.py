"""Scene bands, song colors, artwork and navigation reach the shared renderer."""

import struct
from pathlib import Path
from zlib import crc32

from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_chooser import compiler, setup_bundle, songs


def test_song_styles_supply_patch_defaults_and_preserve_overrides(
    tmp_path: Path, file_regression: FileRegressionFixture
) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    source = {
        'pages': {'menu': {'type': 'menu'}, 'patches': {'type': 'patch-chooser'}},
        'regions': {'main': {'pages': ['menu', 'patches']}},
        'patch_cycles': {'Sounds': [{'output': 'din', 'channel': 1, 'program': 99}]},
        'use_animations': False,
        'navigation': {
            'song_forward': 'f',
            'song_backward': 'c',
            'page_forward': 'b+e',
            'page_back': 'a+d',
        },
    }
    raw = compiler.compile_show(source, {}, [], target)
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build({'show': raw}, {'show': source}, target.memory).pack()
    )
    scene = {
        'display_name': 'Song controls',
        'bindings': {
            b: {
                'output': 'din',
                'channel': 1,
                'program': i + 1,
                'name': f'Sound {i + 1}',
            }
            for i, b in enumerate('abcdef')
        },
        'binding_view': {'status': 'Ready'},
    }
    artwork = tmp_path / 'light.png'
    Image.new('RGB', (160, 64), 'red').save(artwork)
    library = songs.Library.model_validate(
        {
            'patches': {
                'scene': scene,
                'override': scene
                | {
                    'style': {'foreground_color': 'red', 'font_name': 'CaveatBold'},
                    'navigation': {'page_forward': 'a+e'},
                },
                'ordinary': {
                    'bindings': scene['bindings'],
                    'style': {'foreground_color': 'red'},
                },
            },
            'songs': [
                {
                    'name': 'Our song',
                    'display_name': 'Our show',
                    'image': str(artwork),
                    'navigation': {'page_forward': 'd+e'},
                    'enter': {'output': 'din', 'channel': 1, 'program': 11},
                    'exit': {'output': 'din', 'channel': 1, 'program': 12},
                    'patches': ['scene', 'override', 'ordinary'],
                    'style': {
                        'foreground_color': 'navy',
                        'background_color': '0xFFFFF0',
                        'font_name': 'CourierPrime',
                    },
                },
                {
                    'name': 'Next song',
                    'patches': ['scene'],
                    'style': {
                        'foreground_color': 'white',
                        'background_color': 'navy',
                        'font_name': 'CaveatBold',
                    },
                },
                {
                    'name': 'Final song',
                    'patches': ['scene'],
                    'style': {'background_color': 'blue', 'font_name': 'CourierPrime'},
                },
            ],
            'setlists': [
                {'name': 'Tonight', 'songs': ['Our song', 'Next song', 'Final song']}
            ],
        }
    )
    data = songs.encode(library, target)
    header = struct.pack('<4sIIII', b'CSG8', 1, len(data), crc32(data), 0)
    storage = tmp_path / 'session'
    folder = storage / 'cntrl/songs'
    folder.mkdir(parents=True)
    (folder / '0.bin').write_bytes(
        header + struct.pack('<I', crc32(header)) + data + b'DONE'
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
    image = Image.new('RGB', (480, 768))
    with backend.Session(layout, target, executable, storage, bundle) as session:

        def press(button: str) -> None:
            session.button(button, True)
            session.advance(session.time_us + 60000)
            session.button(button, False)
            session.advance(session.time_us + 120000)

        def snapshot(row: int) -> None:
            directory = tmp_path / f'view-{row}'
            cli.save(session, directory)
            for i in range(3):
                with Image.open(directory / f'lcd-{i + 1}.png') as panel:
                    image.paste(panel, (i * 160, row * 128))

        session.advance(1000000)
        press('c')  # Pages and tools.
        press('b')
        press('c')  # Songs and setlists.
        press('b')
        press('c')  # Setlists.
        press('c')  # Choose Tonight.
        press('d')  # Set edit menu.
        for _ in range(7):
            press('b')
        press('c')  # Save Tonight as the device-wide startup set.
        assert not [m for m in session.messages if m.kind in ('midi', 'error')]

    with backend.Session(layout, target, executable, storage, bundle) as session:
        session.advance(1000000)
        assert not [m for m in session.messages if m.kind in ('midi', 'error')]
        snapshot(0)
        press('f')  # First song step installs the scene.
        assert [m.data for m in session.messages if m.kind == 'midi'] == [b'\xc0\x0a']
        snapshot(1)
        first = dict(session.frames)
        assert first['lcd-2'][(48 * 160 + 8) * 2 :][:2] == b'\xf8\x00'
        left = first['lcd-1']
        assert left[:2] == b'\0\0'  # The rounded corner is clipped.
        upper_edge = (32 * 160 + 8) * 2
        lower_edge = (96 * 160 + 8) * 2
        assert left[upper_edge - 320 :][:2] == b'\0\x10'
        assert left[upper_edge:][:2] != b'\0\x10'
        assert left[lower_edge - 320 :][:2] != b'\0\x10'
        assert left[lower_edge:][:2] == b'\0\x10'
        assert left[(8 * 160 + 8) * 2 :][:2] == b'\0\x10'
        assert left[(119 * 160 + 8) * 2 :][:2] == b'\0\x10'
        assert left[(48 * 160 + 8) * 2 :][:2] != b'\0\x10'

        def chord(a: str, b: str) -> None:
            session.button(a, True)
            session.advance(session.time_us + 60000)
            press(b)
            session.button(a, False)
            session.advance(session.time_us + 120000)

        count = len([m for m in session.messages if m.kind == 'midi'])
        chord('d', 'e')
        assert session.frames != first
        chord('a', 'd')
        assert dict(session.frames) == first
        press('f')
        assert session.frames != first
        second = dict(session.frames)
        chord('a', 'e')
        assert session.frames != second
        chord('a', 'd')
        assert dict(session.frames) == second
        press('c')
        assert dict(session.frames) == first
        assert len([m for m in session.messages if m.kind == 'midi']) == count
        press('a')
        assert any(m.kind == 'midi' and m.data[:1] == b'\xc0' for m in session.messages)
        press('f')  # Explicit patch foreground overrides the song foreground.
        snapshot(2)
        assert session.frames != first
        press('f')  # Ordinary switch views inherit the same song defaults.
        snapshot(3)
        press('f')  # Empty position restores the overview and previews Next song.
        assert [m.data for m in session.messages if m.kind == 'midi'][-1] == b'\xc0\x0b'
        snapshot(4)
        press('f')  # The next song installs its controls with its own defaults.
        assert not [m for m in session.messages if m.kind == 'error']
        assert session.frames['lcd-2'][(48 * 160 + 8) * 2 :][:2] == b'\0\x10'
        press('f')  # The shared boundary separates the songs.
        press('f')  # Final song installs its own display defaults.
        assert session.frames['lcd-2'][(48 * 160 + 8) * 2 :][:2] == b'\0\x1f'
        midi_count = len([m for m in session.messages if m.kind == 'midi'])
        session.button('c', True)
        session.advance(session.time_us + 600000)
        session.button('c', False)
        session.advance(session.time_us + 120000)
        for _ in range(5):
            press('b')
        press('c')  # Switch to Patch Chooser through Pages and tools.
        press('a')  # Recall the chooser's patch using the pedal's physical label.
        snapshot(5)
        midi = [m.data for m in session.messages if m.kind == 'midi']
        assert len(midi) == midi_count + 1 and midi[-1] == b'\xc0\x62'
        chord('b', 'e')  # Use the setup's page-forward assignment to open Menu.
        press('c')
        press('b')
        press('c')  # Songs and setlists.
        press('c')  # Resume performance without moving the cursor.
        assert len([m for m in session.messages if m.kind == 'midi']) == len(midi)
        assert not [m for m in session.messages if m.kind == 'error']
    path = tmp_path / 'rendered-song-styles.png'
    image.save(path)
    file_regression.check(
        path.read_bytes(), binary=True, extension='.png', basename='song-styles'
    )


BASE = Path(__file__).parents[1]
