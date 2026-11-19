"""Song styles reach the real song browser and full-pedal patch renderer."""

import struct
from pathlib import Path
from zlib import crc32

from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_switcher import compiler, setup_bundle, songs


def test_song_styles_supply_patch_defaults_and_preserve_overrides(
    tmp_path: Path, file_regression: FileRegressionFixture
) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    source = {'pages': {'menu': {'type': 'menu'}}, 'use_animations': False}
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
    library = songs.Library.model_validate(
        {
            'patches': {
                'scene': scene,
                'override': scene
                | {'style': {'foreground_color': 'red', 'font_name': 'CaveatBold'}},
                'ordinary': {
                    'bindings': scene['bindings'],
                    'style': {'foreground_color': 'red'},
                },
            },
            'songs': [
                {
                    'name': 'Our song',
                    'patches': ['scene', 'override', 'ordinary'],
                    'style': {
                        'foreground_color': 'navy',
                        'background_color': '0xFFFFF0',
                        'font_name': 'CourierPrime',
                    },
                },
                {
                    'name': 'Next song',
                    'add_last_step': False,
                    'patches': [{'type': 'delay', 'duration': '1 ms'}],
                    'style': {
                        'foreground_color': 'white',
                        'background_color': 'navy',
                        'font_name': 'CaveatBold',
                    },
                },
                {
                    'name': 'Final song',
                    'patches': [{'type': 'delay', 'duration': '1 ms'}],
                    'style': {'background_color': 'blue', 'font_name': 'CourierPrime'},
                },
            ],
            'setlists': [
                {'name': 'Tonight', 'songs': ['Our song', 'Next song', 'Final song']}
            ],
        }
    )
    data = songs.encode(library, target)
    header = struct.pack('<4sIIII', b'CSG7', 1, len(data), crc32(data), 0)
    storage = tmp_path / 'session'
    folder = storage / 'cntrl/songs'
    folder.mkdir(parents=True)
    (folder / '0.bin').write_bytes(
        header + struct.pack('<I', crc32(header)) + data + b'DONE'
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
    image = Image.new('RGB', (480, 640))
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
        snapshot(0)
        press('f')  # First song step installs the full-pedal patch.
        snapshot(1)
        first = dict(session.frames)
        press('a')
        assert any(m.kind == 'midi' and m.data[:1] == b'\xc0' for m in session.messages)
        press('f')  # Explicit patch foreground overrides the song foreground.
        snapshot(2)
        assert session.frames != first
        press('f')  # Ordinary switch views inherit the same song defaults.
        snapshot(3)
        press('f')  # Last step restores the song overview and previews Next song.
        snapshot(4)
        press('f')  # A MIDI-only step retains controls with new song defaults.
        assert session.frames['lcd-2'][160:162] == b'\0\x10'
        press('f')  # No last step or replacement control map forces this redraw.
        assert session.frames['lcd-2'][160:162] == b'\0\x1f'
        assert not [m for m in session.messages if m.kind == 'error']
    path = tmp_path / 'rendered-song-styles.png'
    image.save(path)
    file_regression.check(
        path.read_bytes(), binary=True, extension='.png', basename='song-styles'
    )


BASE = Path(__file__).parents[1]
