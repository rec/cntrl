"""Song steps share the firmware input, binding and performance UI paths."""

import subprocess
from pathlib import Path

from cntrl.fonts import ui
from cntrl.patch_chooser import compiler, compiler_inputs, models, songs
from scripts.native_tests import NativePrograms


def test_song_step_navigation_and_boundaries(
    tmp_path: Path, native_programs: NativePrograms
) -> None:
    target = models.Target.model_validate(
        compiler_inputs.read_document(Path('firmware/test/target.json'))
    )
    font = tmp_path / 'Arimo.cfont'
    font.write_bytes(
        ui.prepare('Arimo', [16], characters=' ABCFcontrol-change0123456789')
    )
    target = target.model_copy(update={'font_sources': {'Arimo': str(font)}})
    common = {
        'A': {
            'mode': 'momentary',
            'on': {'controller': 64, 'value': 127},
            'off': {'controller': 64, 'value': 0},
        },
        'B': {'controller': 7, 'value': 100},
        'C': {'controller': 99, 'value': 1},
        'F': {'controller': 98, 'value': 1},
    }
    library = songs.Library(
        instruments={'synth': {'output': 'din', 'channel': 1}},
        patches={
            'forward': {'operation': 'next-song-step'},
            'backward': {'operation': 'previous-song-step'},
        },
        songs=[
            songs.Song(
                name='One',
                patches=[
                    {
                        'controller': 20,
                        'value': n,
                        'bindings': common,
                        'binding_view': {'status': 'A'},
                    }
                    for n in (1, 2, 3)
                ],
            ),
            songs.Song(
                name='Two',
                patches=[
                    {
                        'controller': 20,
                        'value': n,
                        'bindings': common,
                        'binding_view': {'status': 'A'},
                    }
                    for n in (4, 5)
                ],
            ),
        ],
        setlists=[songs.Setlist(name='Set', songs=['One', 'Two', 'One'])],
    )
    encoded = songs.encode(library, target)
    restored = songs.decode(encoded)
    assert [len(s.patches) for s in restored.songs] == [3, 2]
    fixture = tmp_path / 'songs.bin'
    fixture.write_bytes(encoded)
    setup = tmp_path / 'setup.bin'
    setup.write_bytes(compiler.compile_show({}, {}, [], target))
    runner = native_programs.build('test/test_song_steps.cpp')
    subprocess.run([str(runner), str(fixture), str(setup)], check=True)
    hooked = library.model_copy(
        update={
            'songs': [
                s.model_copy(
                    update={
                        'enter': {
                            'type': 'control-change',
                            'controller': 21,
                            'value': 11 + i * 10,
                        },
                        'exit': {
                            'type': 'control-change',
                            'controller': 21,
                            'value': 12 + i * 10,
                        },
                    }
                )
                for i, s in enumerate(library.songs)
            ]
        }
    )
    encoded = songs.encode(hooked, target)
    assert [
        (s.enter['value'], s.exit['value'])
        for s in songs.decode(encoded).songs
        if s.enter is not None and s.exit is not None
    ] == [(11, 12), (21, 22)]
    fixture.write_bytes(encoded)
    subprocess.run([str(runner), str(fixture), str(setup), '--hooks'], check=True)
