"""Change setups through real emulator buttons while MIDI generators run."""

from pathlib import Path

import tomlkit

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_switcher import compiler, setup_bundle


def test_setup_change_stops_looping_file_and_lfo(tmp_path: Path) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    sources = {
        n: compiler.read_document(BASE / 'emulator/setup-change' / f'{n}.toml')
        for n in ('active', 'quiet')
    }
    compiled = setup_bundle.reindex(
        {
            n: compiler.compile_show(s, {}, [], target, documents=sources)
            for n, s in sources.items()
        }
    )
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(setup_bundle.build(compiled, sources, target.memory).pack())
    storage = tmp_path / 'session'
    midi = storage / 'midi'
    midi.mkdir(parents=True)
    # Format 0, 480 PPQN: note 60 held for half a second at default 120 BPM.
    track = b'\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00'
    (midi / 'loop.mid').write_bytes(
        b'MThd\x00\x00\x00\x06\x00\x00\x00\x01\x01\xe0'
        + b'MTrk'
        + len(track).to_bytes(4, 'big')
        + track
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
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

        advance(1200000)
        playing = [e for e in session.messages if e.kind == 'midi']
        assert sum(e.data == b'\x90\x3c\x64' for e in playing) >= 3
        assert len({e.data[2] for e in playing if e.data[:2] == b'\xb0\x4a'}) > 10
        cli.save(session, tmp_path / 'playing')
        press('a')  # Run the menu patch.
        press('c')  # Setups.
        press('b')  # Browse from active to quiet.
        press('c')  # Actions for quiet.
        cli.save(session, tmp_path / 'choosing')
        notes = [
            e for e in session.messages if e.data in (b'\x90\x3c\x64', b'\x80\x3c\x00')
        ]
        assert notes[-1].data == b'\x90\x3c\x64'  # Change with a note still held.
        assert any(
            e.data[:2] == b'\xb0\x4a' and e.time_us >= now - 20000
            for e in session.messages
        )  # The LFO also remains active in the menu.
        start = len(session.messages)
        press('c')  # Select quiet while both generators are still running.
        advance(500000)
        cleanup = session.messages[start:]
        assert any(e.data == b'\x80\x3c\x00' for e in cleanup)
        assert any(e.data[:2] == b'\xb0\x7b' for e in cleanup)  # All notes off.
        assert any(e.data == b'\xb0\x4a\x00' for e in cleanup)  # LFO off value.
        cli.save(session, tmp_path / 'changed')
        start = len(session.messages)
        advance(2000000)
        assert not [e for e in session.messages[start:] if e.kind == 'midi']
        press('a')  # The destination setup's patch must work.
        assert any(e.data == b'\xb0\x07\x63' for e in session.messages[start:])
        cli.save(session, tmp_path / 'finished')
        (tmp_path / 'replay.toml').write_text(
            tomlkit.dumps(
                {
                    'until': now / 1000000,
                    'events': [e.model_dump() for e in session.events],
                }
            )
        )
        assert not [e.message for e in session.messages if e.kind == 'error']
        assert session.memory()['failures'] == 0


BASE = Path(__file__).parents[1]
