"""Song steps share the firmware input, binding and performance UI paths."""

import subprocess
from pathlib import Path

import tomlkit

from cntrl.fonts import ui
from cntrl.patch_chooser import compiler, compiler_inputs, models, resolved, songs
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
            'off': {'controller': 64},
        },
        'B': {'controller': 7, 'value': 100},
        'C': {'name': 'Accent', 'on': {'controller': 99, 'value': 1}},
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
                        'bindings': {k: v for k, v in common.items() if k != 'C'},
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
    pages = {'pages': {'Live': {'type': 'patch-chooser'}}}
    setup.write_bytes(
        compiler.compile_show(pages, {}, [], target, setting_overrides={})
    )
    runner = native_programs.build('test/test_song_steps.cpp')
    subprocess.run([str(runner), str(fixture), str(setup)], check=True)
    edited = tmp_path / 'edited.bin'
    layout_setup = tmp_path / 'layout.bin'
    layout_setup.write_bytes(
        compiler.compile_show(
            {
                'pages': {
                    'Time': {'type': 'time', 'ram2_bytes': 16},
                    'Language': {'type': 'language'},
                    'Empty': {'type': 'None'},
                    'Status': {'type': 'status'},
                    'Gap': {'type': 'None'},
                    'Browser': {'type': 'patch-chooser', 'min_displays': 0},
                    'Other status': {'type': 'status'},
                },
                'regions': {
                    'tools': {'pages': ['Time', 'Language', 'Empty', 'Other status']},
                    'performance': {'pages': ['Status', 'Browser']},
                    'extra': {'pages': ['Gap']},
                },
                'layout': {'focus': 'performance'},
            },
            {},
            [],
            target,
        )
    )
    subprocess.run(
        [str(runner), str(fixture), str(layout_setup), '--page-layout', str(edited)],
        check=True,
    )
    saved_layout = tomlkit.parse(resolved.export(edited.read_bytes()))
    assert saved_layout['regions']['tools']['pages'] == [
        'Language',
        'STime',
        'Other status',
    ]
    assert saved_layout['regions']['performance']['pages'] == [
        'Status',
        'Empty',
        'Browser',
        'XStatus',
    ]
    assert saved_layout['pages']['XStatus']['type'] == 'status'
    assert saved_layout['pages']['STime']['ram2_bytes'] == 16
    assert saved_layout['pages']['Browser']['min_displays'] == 1
    assert saved_layout['pages']['Browser']['give_up_displays'] == [
        'center',
        'left',
        'right',
    ]
    assert saved_layout['regions']['Omitted']['pages'] == ['Gap']
    assert saved_layout['layout'] == {
        'order': ['performance', 'Omitted', 'tools'],
        'focus': 'performance',
        'left_wins': True,
    }
    subprocess.run(
        [str(runner), str(fixture), str(setup), '--setup-preferences', str(edited)],
        check=True,
    )
    edited_setup = edited.read_bytes()
    saved = tomlkit.parse(resolved.export(edited_setup))
    assert saved['frame_buffers'] == 2
    assert saved['use_animations'] is False
    assert resolved.refresh_memory(edited_setup, {}, target=target) == edited_setup
    assert saved['pickup'] is True
    assert saved['pickup_pairing_ms'] == 3
    assert saved['loop_stop_reset_controls'] is True
    (neutral,) = saved['loop_stop_neutral_values']
    assert neutral == {
        'output': 'din',
        'channel': 1,
        'type': 'control-change',
        'controller': 1,
        'value': 2,
        'resolution': 7,
    }
    saved_time = saved['page_settings']
    assert saved_time['clock_outputs'] == ['din']
    assert saved_time['clock_timeout'] == 0.26
    assert saved_time['autoselect_clock'] is False
    assert saved_time['clock_bpms'] == [120, 20]
    assert saved_time['title_font'] == 'CourierPrime'
    assert saved_time['ring_color'] == '0x080400'
    midi_setup = tmp_path / 'midi.bin'
    midi_setup.write_bytes(
        compiler.compile_show(
            {
                **pages,
                'pickup': True,
                'pickup_pairing_ms': 9,
                'patches': {
                    'held': {
                        'output': 'din',
                        'channel': 1,
                        'controller': 7,
                        'value': 12,
                        'pickup': True,
                    }
                },
                'loop_stop_reset_controls': True,
                'loop_stop_neutral_values': [
                    {
                        'type': 'control-change',
                        'output': 'din',
                        'channel': 1,
                        'controller': 7,
                        'value': 12,
                    }
                ],
            },
            {},
            [],
            target,
        )
    )
    subprocess.run(
        [str(runner), str(fixture), str(midi_setup), '--midi-preferences', str(edited)],
        check=True,
    )
    saved_midi = tomlkit.parse(resolved.export(edited.read_bytes()))
    assert saved_midi['pickup'] is False
    assert saved_midi['pickup_pairing_ms'] == 2
    assert saved_midi['patches']['held']['pickup'] is True
    assert saved_midi['loop_stop_reset_controls'] is False
    assert saved_midi['loop_stop_neutral_values'][0]['value'] == 12
    counter_setup = tmp_path / 'counters.bin'
    counter_setup.write_bytes(
        compiler.compile_show(
            {
                **pages,
                'counters': {
                    'Scenes': {
                        'type': 'control-change',
                        'output': 'din',
                        'channel': 1,
                        'controller': 7,
                        'minimum': 1,
                        'initial': 2,
                        'maximum': 10,
                    }
                },
                'patches': {
                    'advance': {'operation': 'scroll-counter', 'counter': 'Scenes'}
                },
            },
            {},
            [],
            target,
        )
    )
    subprocess.run(
        [
            str(runner),
            str(fixture),
            str(counter_setup),
            '--counter-settings',
            str(edited),
        ],
        check=True,
    )
    saved_counter = tomlkit.parse(resolved.export(edited.read_bytes()))['counters'][
        'Scenes'
    ]
    assert (
        saved_counter['minimum'],
        saved_counter['initial'],
        saved_counter['step'],
    ) == (
        3,
        4,
        -1,
    )
    subprocess.run(
        [str(runner), str(fixture), str(setup), '--song-settings'], check=True
    )
    styled = library.model_copy(
        update={
            'songs': [
                library.songs[0].model_copy(
                    update={
                        'display_name': 'Écho',
                        'style': models.Style(
                            foreground_color='blue',
                            background_color='white',
                            font_name='Arimo',
                        ),
                    }
                ),
                library.songs[1].model_copy(
                    update={'style': models.Style(foreground_color='black')}
                ),
            ],
            'setlists': [
                library.setlists[0].model_copy(
                    update={
                        'display_name': 'Friday',
                        'style': models.Style(
                            foreground_color='white', background_color='black'
                        ),
                    }
                )
            ],
        }
    )
    fixture.write_bytes(songs.encode(styled, target))
    subprocess.run(
        [str(runner), str(fixture), str(setup), '--song-appearance', str(edited)],
        check=True,
    )
    restored_style = songs.decode(edited.read_bytes())
    first, second = restored_style.songs
    assert first.name == 'One' and first.display_name == '!SÉcho'
    assert first.style == models.Style(
        foreground_color='blue',
        background_color='0xF6FFFF',
        font_name='AtkinsonNextSemiBold',
    )
    assert second.style == models.Style(foreground_color='0x080000')
    (setlist,) = restored_style.setlists
    assert setlist.name == 'Set' and setlist.display_name == 'LFriday'
    assert setlist.songs == ['One', 'Two', 'One']
    assert setlist.style == models.Style(
        foreground_color='white', background_color='0x080000'
    )
    with_spare = library.model_copy(
        update={
            'setlists': [
                *library.setlists,
                songs.Setlist(name='Friday', songs=['One', 'Two', 'One']),
            ]
        }
    )
    fixture.write_bytes(songs.encode(with_spare, target))
    subprocess.run(
        [str(runner), str(fixture), str(setup), '--setlist-settings'], check=True
    )
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
    fixture.write_bytes(songs.encode(library, target))
    setup.write_bytes(
        compiler.compile_show(
            {**pages, 'navigation': {'song_forward': 'C+F'}}, {}, [], target
        )
    )
    subprocess.run(
        [str(runner), str(fixture), str(setup), '--shortcut-conflict'],
        check=True,
    )
    setup.write_bytes(
        compiler.compile_show(
            {
                **pages,
                'instruments': {
                    'synth': {'instrument': 'synth', 'output': 'din', 'channel': 1},
                    'alternate': {
                        'instrument': 'alternate',
                        'output': 'din',
                        'channel': 2,
                    },
                },
                'definitions': {
                    'profiles': [
                        {
                            'instrument': 'synth',
                            'name': 'default',
                            'banks': {1: {'lsb': 7}, 3: {'msb': 4, 'lsb': 9}},
                        },
                        {
                            'instrument': 'alternate',
                            'name': 'default',
                            'banks': {2: {'lsb': 2}},
                        },
                    ],
                },
                'patches': {
                    'Lead': {
                        'type': 'control-change',
                        'output': 'din',
                        'channel': 1,
                        'controller': 20,
                        'value': 10,
                    },
                    'Voice': {'instrument': 'synth', 'program': 12, 'bank': 1},
                    'Scene': {
                        'patch': 'Lead',
                        'bindings': {
                            'A': {
                                'mode': 'momentary',
                                'on': {
                                    'output': 'din',
                                    'channel': 1,
                                    'controller': 64,
                                    'value': 127,
                                },
                                'off': {
                                    'output': 'din',
                                    'channel': 1,
                                    'controller': 64,
                                },
                            },
                        },
                    },
                    'Other': {'instrument': 'alternate', 'program': 12, 'bank': 2},
                    'Direct': {
                        'type': 'program-change',
                        'output': 'din',
                        'channel': 3,
                        'program': 12,
                    },
                },
            },
            {},
            [],
            target,
        )
    )
    subprocess.run(
        [str(runner), str(fixture), str(setup), '--named-patch-settings'],
        check=True,
    )
