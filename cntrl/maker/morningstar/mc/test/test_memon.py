"""Play and reopen Memon through the shared firmware emulator backend."""

import struct
from pathlib import Path
from typing import cast

import tomlkit

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_chooser import compiler, compiler_inputs, resolved, setup_bundle


def test_memon_uses_three_displays_and_recovers_high_score(tmp_path: Path) -> None:
    target = cli.load_target(profiles.export(None, 'mc6_pro', tmp_path / 'profile'))
    source = cast(
        'dict[str, object]', compiler_inputs.read_document(BASE / 'emulator/memon.toml')
    )
    assert isinstance(source, dict)
    source['page_settings'] = {'memon': {'channel': 3, 'timeout': 1.0}}
    raw = compiler.compile_show(source, {}, [], target)
    restored = tomlkit.parse(resolved.export(raw)).unwrap()
    assert restored['page_settings']['memon']['channel'] == 3
    assert restored['page_settings']['memon']['timeout'] == 1.0
    assert restored['pages']['Memon']['min_displays'] == 3
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build({'memon': raw}, {'memon': source}, target.memory).pack()
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
    directory = tmp_path / 'session'
    with backend.Session(layout, target, executable, directory, bundle) as session:

        def tap(button: str) -> None:
            session.button(button, True)
            session.advance(session.time_us + 60_000)
            session.button(button, False)
            session.advance(session.time_us + 60_000)

        def packets() -> list[bytes]:
            return [
                m.data
                for m in session.messages
                if m.kind == 'midi' and m.endpoint == 'din'
            ]

        session.advance(1_000_000)
        idle = dict(session.frames)
        assert len(idle) == 3 and all(
            frame[:2] == b'\x00\x00' for frame in idle.values()
        )
        assert all(any(frame) for frame in idle.values())
        tap('c')
        session.advance(1_210_000)
        cue = packets()[-1]
        assert cue[0] == 0x92 and cue[1] in (60, 64, 67, 70)
        assert session.frames['lcd-2'] != idle['lcd-2']
        session.advance(1_350_000)
        assert packets()[-1] == bytes((0x82, cue[1], 0))
        tap('deab'[(60, 64, 67, 70).index(cue[1])])
        assert any(
            struct.unpack_from('<I', f.read_bytes(), 4)[0] == 1
            for f in directory.glob('cntrl/memon-*.bin')
        )
        session.advance(4_000_000)
        assert packets()[-4:] == [bytes((0x92, n, 100)) for n in (60, 64, 67, 70)]
        tap('f')
        assert packets()[-4:] == [bytes((0x82, n, 0)) for n in (60, 64, 67, 70)]
        assert not [m.message for m in session.messages if m.kind == 'error']

    with backend.Session(layout, target, executable, directory, bundle) as session:
        session.advance(1_000_000)
        restored_frame = session.frames['lcd-1']
        # A fresh run resets the score and round. Only the high-score row differs.
        assert restored_frame[: 26 * 160 * 2] != idle['lcd-1'][: 26 * 160 * 2]
        assert restored_frame[26 * 160 * 2 :] == idle['lcd-1'][26 * 160 * 2 :]


BASE = Path(__file__).parents[1]
