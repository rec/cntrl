"""Exercise a persistent song MIDI scope through the shared runtime."""

import struct
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_chooser import compiler, device_globals, setup_bundle, songs
from cntrl.patch_chooser.global_overlay import Overlay


@pytest.mark.parametrize('include_empty', [False, True])
def test_setlist_page_cycle_respects_an_explicit_empty(
    tmp_path: Path, include_empty: bool
) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    pages = {
        **({'blank': {'type': 'None'}} if include_empty else {}),
        'status': {'type': 'status'},
        'language': {'type': 'language'},
    }
    source = {'pages': pages, 'use_animations': False}
    raw = compiler.compile_show(source, {}, [], target)
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build({'show': raw}, {'show': source}, target.memory).pack()
    )
    library = songs.Library.model_validate(
        {
            'songs': [{'name': 'First', 'patches': [{'duration': '1 ms'}]}],
            'setlists': [{'name': 'Tonight', 'songs': ['First']}],
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
    folder = storage / 'cntrl/global'
    folder.mkdir()
    (folder / '0.bin').write_bytes(
        device_globals.journal(Overlay(startup_set='Tonight'))
    )

    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
    with backend.Session(layout, target, executable, storage, bundle) as session:
        session.advance(1_000_000)
        expected = [
            'status',
            'language',
            'songs-and-setlists' if include_empty else 'status',
        ]
        for page in expected:
            session.button('f', True)
            session.advance(session.time_us + 600_000)
            session.button('f', False)
            session.advance(session.time_us + 60_000)
            assert session.tooltip_context[0] == page


def test_song_lfo_keeps_phase_across_steps_and_stops_at_boundary(
    tmp_path: Path,
) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    song_routes = {'sweep': lfo(21, [0, 100], 127)}
    source = {
        'pages': {
            'songs': {'type': 'songs-and-setlists'},
            'patches': {'type': 'patch-chooser'},
        },
        'regions': {'main': {'pages': ['songs', 'patches']}},
        'patches': {
            'external': {'patches': [], 'midi': {'sweep': lfo(22, [24, 24], 0)}},
        },
        'midi': {'background': lfo(20, [50, 50], 0)},
        'use_animations': False,
        'navigation': {'song_forward': 'f', 'song_backward': 'c'},
    }
    library = songs.Library.model_validate(
        {
            'songs': [
                {
                    'name': 'First',
                    'midi': song_routes,
                    'patches': [
                        {'document': 'show', 'patch': 'external'},
                        {'duration': '1 ms', 'midi': {'sweep': lfo(23, [25, 25], 0)}},
                    ],
                },
                {'name': 'Second', 'patches': [{'duration': '1 ms'}]},
            ],
            'setlists': [{'name': 'Tonight', 'songs': ['First', 'Second']}],
        }
    )
    documents: dict[str, object] = {'show': source}
    raw = compiler.compile_show(source, {}, [], target)
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build({'show': raw}, documents, target.memory).pack()
    )
    data = songs.encode(library, target, documents)
    assert songs.decode(data).songs[0].midi == song_routes
    header = struct.pack('<4sIIII', b'CSG8', 1, len(data), crc32(data), 0)
    storage = tmp_path / 'session'
    folder = storage / 'cntrl/songs'
    folder.mkdir(parents=True)
    (folder / '0.bin').write_bytes(
        header + struct.pack('<I', crc32(header)) + data + b'DONE'
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
    with backend.Session(layout, target, executable, storage, bundle) as session:
        session.advance(1_000_000)
        for button in ('c', 'b', 'c', 'c', 'd', *'bbbbbbb', 'c'):
            press(session, button)

    with backend.Session(layout, target, executable, storage, bundle) as session:
        session.advance(1_000_000)
        assert cc_values(session, 20) == [50]
        assert cc_values(session, 21) == []
        press(session, 'f')
        assert cc_values(session, 21)[0] == 0
        assert cc_values(session, 22) == [24]
        press(session, 'f')
        assert cc_values(session, 22) == [24, 0]
        assert cc_values(session, 23) == [25]
        press(session, 'c')
        assert cc_values(session, 22) == [24, 0, 24]
        assert cc_values(session, 23) == [25, 0]
        sweep = cc_values(session, 21)
        assert len(sweep) > 3
        assert sweep == sorted(set(sweep))
        press(session, 'c')
        assert cc_values(session, 21)[-1] == 127
        session.advance(session.time_us + 200_000)
        assert cc_values(session, 21).count(127) == 1
        end = len(cc_values(session, 21))
        press(session, 'f')
        assert cc_values(session, 21)[end] == 0
        press(session, 'f')
        press(session, 'f')
        assert cc_values(session, 21)[-1] == 127
        end = len(cc_values(session, 21))
        press(session, 'f')
        assert len(cc_values(session, 21)) == end
        assert cc_values(session, 20) == [50]
        assert not [m for m in session.messages if m.kind == 'error']


def lfo(controller: int, endpoints: list[int], final: int) -> dict[str, object]:
    return {
        'to': 'din',
        'generate': {
            'lfo': {
                'target': {
                    'type': 'control-change',
                    'channel': 1,
                    'controller': controller,
                },
                'shape': 'saw-up',
                'range': endpoints,
                'period': '4 s',
                'update_interval': '50 ms',
                'off': {'action': 'value', 'value': final},
            }
        },
    }


def press(session: backend.Session, button: str) -> None:
    session.button(button, True)
    session.advance(session.time_us + 60_000)
    session.button(button, False)
    session.advance(session.time_us + 120_000)


def cc_values(session: backend.Session, controller: int) -> list[int]:
    return [
        m.data[2]
        for m in session.messages
        if m.kind == 'midi' and m.data[:2] == bytes((0xB0, controller))
    ]


BASE = Path(__file__).parents[1]
