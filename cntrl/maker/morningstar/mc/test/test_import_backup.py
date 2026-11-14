import json
import struct
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.maker.morningstar.mc.import_backup import convert
from cntrl.patch_switcher import compiler, resolved, setup_bundle


def test_saved_vl70_bank_converts_to_editable_toml(
    file_regression: FileRegressionFixture,
) -> None:
    setup = convert(json.loads(BACKUP.read_bytes()), 0)
    file_regression.check(
        tomlkit.dumps(
            setup.model_dump(mode='json', exclude_none=True, exclude_defaults=True)
        ),
        fullpath=BASE / 'emulator' / 'vl70-1.toml',
    )


def test_unsupported_messages_are_reported_at_the_source_slot() -> None:
    document = json.loads(BACKUP.read_bytes())
    document['data']['bankArray'][0]['presetArray'][0]['msgArray'][0]['t'] = 99
    with pytest.raises(
        ValueError, match='/presetArray/0/msgArray/0: unsupported message type 99'
    ):
        convert(document, 0)


def test_imported_bank_runs_counters_and_bank_transitions(
    tmp_path: Path, file_regression: FileRegressionFixture
) -> None:
    target_path = profiles.export(
        BASE / 'patch-switcher-target.json', 'mc6_pro', tmp_path / 'profile'
    )
    target = cli.load_target(target_path)
    show = convert(json.loads(BACKUP.read_bytes()), 0).model_dump(exclude_none=True)
    raw = compiler.compile_show(show, {}, [], target)
    restored = tomlkit.parse(resolved.export(raw)).unwrap()
    assert compiler.compile_show(restored, {}, [], target) == raw
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build({'vl70-1': raw}, {'vl70-1': show}, target.memory).pack()
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator' / 'mc6-pro.toml')
    storage = tmp_path / 'session'
    (storage / 'cntrl').mkdir(parents=True)
    calibration = struct.pack('<IHH', 1, 0, 1023)
    for i in (1, 2):
        (storage / 'cntrl' / f'expression-{i}.cal').write_bytes(
            calibration + struct.pack('<I', crc32(calibration))
        )
    with backend.Session(layout, target, executable, storage, bundle) as session:
        now = 0

        def press(button: str) -> None:
            nonlocal now
            now += 20000
            session.advance(now)
            session.button(button, True)
            now += 60000
            session.advance(now)
            session.button(button, False)
            now += 60000
            session.advance(now)

        press('a')  # Activate the imported bank; no MIDI on bank entry.
        assert not [e for e in session.messages if e.kind == 'midi']
        press('a')  # Counter 3: 21 -> 22, on both channels and both outputs.
        press('a')
        for button in 'bdef':
            press(button)
        press('c')  # Enter VL70-2.
        press('a')  # Counter 8: 46 -> 47.
        for button in 'bdef':
            press(button)
        press('c')  # Return to VL70-1 without resetting its counters.
        press('a')  # Counter 3: 23 -> 24.
        press('a')  # 25.
        press('a')  # Wrap to 21.
        din = [
            list(e.data)
            for e in session.messages
            if e.kind == 'midi' and e.endpoint == 'din'
        ]
        usb = [
            list(e.data)
            for e in session.messages
            if e.kind == 'midi' and e.endpoint == 'usb-device'
        ]
        file_regression.check(tomlkit.dumps({'din': din}), extension='.toml')
        assert usb == din
        # The backup sends counter 8 on Ch1 and counter 3 on Ch2 in VL70-2.
        assert din[12] == [0xC0, 47] and din[13] == [0xC1, 23]
        for port, controller in ((0, 13), (1, 4)):
            session.expression(port, 0)
            now += 200000
            session.advance(now)
            session.expression(port, 1023)
            now += 200000
            session.advance(now)
            primary = [
                list(e.data)
                for e in session.messages
                if e.kind == 'midi'
                and e.endpoint == 'din'
                and e.data[:2] == bytes((0xB0, controller))
            ]
            mirror = [
                list(e.data)
                for e in session.messages
                if e.kind == 'midi'
                and e.endpoint == 'usb-device'
                and e.data[:2] == bytes((0xB0, controller))
            ]
            assert primary and primary[-1] == [0xB0, controller, 127]
            assert mirror == primary
        assert not [e.message for e in session.messages if e.kind == 'error']
        cli.save(session, tmp_path / 'rendered')


BASE = Path(__file__).parents[1]
BACKUP = BASE / 'Morningstar_MC6PRO_All_Banks_Backup_20261021_135556.json'
