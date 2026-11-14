import json
import struct
import subprocess
import sys
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit
from PIL import Image
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.emulator import backend, cli
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.maker.morningstar.mc.import_backup import ImportBackup, convert, run
from cntrl.patch_switcher import compiler, models, resolved, setup_bundle


def test_saved_vl70_bank_converts_to_editable_toml(
    tmp_path: Path,
    file_regression: FileRegressionFixture,
) -> None:
    setup = convert(json.loads(BACKUP.read_bytes()), 0)
    assert list(setup.patches) == ['Banked']
    imported = models.Banked.model_validate(setup.patches['Banked'])
    assert imported.bank_count == 128 and imported.page_count == 4
    assert imported.banks == {0: {0: 'VL70-1'}, 1: {0: 'VL70-2'}, 2: {0: 'VL70-1'}}
    assert list(imported.patches) == ['VL70-1', 'VL70-2']
    assert setup.patch_cycles == {'Imported': ['Banked']}
    run(ImportBackup(backup=BACKUP, output=tmp_path))
    text = (tmp_path / 'setup.toml').read_text()
    assert models.Setup.model_validate(tomlkit.parse(text).unwrap()).model_dump(
        mode='json'
    ) == setup.model_dump(mode='json')
    file_regression.check(
        text,
        fullpath=BASE / 'emulator' / 'vl70-1.toml',
    )


def test_unsupported_messages_are_reported_at_the_source_slot() -> None:
    document = json.loads(BACKUP.read_bytes())
    document['data']['bankArray'][0]['presetArray'][0]['msgArray'][0]['t'] = 99
    with pytest.raises(
        ValueError, match='/presetArray/0/msgArray/0: unsupported message type 99'
    ):
        convert(document, 0)


def test_sparse_slots_preserve_customisation_and_page_limits(tmp_path: Path) -> None:
    document = json.loads(BACKUP.read_bytes())
    banks = document['data']['bankArray']
    banks[3]['bankName'] = 'Ambient'
    banks[3]['presetArray'][18]['shortName'] = 'Scene'
    banks[4]['bankName'] = 'Quiet'
    banks[5]['pageLimit'] = 2
    banks[6]['bankName'] = 'VL70-1'
    setup = convert(document, 3)
    imported = models.Banked.model_validate(setup.patches['Banked'])
    assert imported.banks[3] == {3: 'Ambient (4)'}
    assert imported.banks[4] == {0: 'Quiet'}
    assert imported.banks[6] == {0: 'VL70-1 (2)'}
    assert 5 not in imported.banks
    assert imported.page_counts == {5: 2}
    assert imported.initial_bank == 3
    target = cli.load_target(
        profiles.export(
            BASE / 'patch-switcher-target.json', 'mc6_pro', tmp_path / 'profile'
        )
    )
    raw = compiler.compile_show(setup.model_dump(exclude_none=True), {}, [], target)
    restored = tomlkit.parse(resolved.export(raw)).unwrap()
    assert compiler.compile_show(restored, {}, [], target) == raw


def test_completely_empty_banked_patch_round_trips(tmp_path: Path) -> None:
    setup = convert(json.loads(BACKUP.read_bytes()), 127)
    patch = models.Banked.model_validate(setup.patches['Banked']).model_copy(
        update={'banks': {}, 'patches': {}}
    )
    setup.patches['Banked'] = patch.model_dump(exclude_none=True)
    target = cli.load_target(
        profiles.export(
            BASE / 'patch-switcher-target.json', 'mc6_pro', tmp_path / 'profile'
        )
    )
    raw = compiler.compile_show(setup.model_dump(exclude_none=True), {}, [], target)
    restored = tomlkit.parse(resolved.export(raw)).unwrap()
    empty = models.Banked.model_validate(restored['patches']['Banked'])
    assert empty.banks == empty.patches == {}
    assert empty.bank_count == 128 and empty.page_count == 4
    assert empty.initial_bank == 127
    assert compiler.compile_show(restored, {}, [], target) == raw


def test_selected_importer_creates_the_requested_initial_bank(tmp_path: Path) -> None:
    output = tmp_path / 'import with spaces'
    subprocess.run(
        [
            sys.executable,
            '-m',
            'cntrl.patch_switcher.cli',
            'import',
            '--target-dir',
            str(BASE),
            '--',
            str(BACKUP),
            '--bank',
            '1',
            '--output',
            str(output),
        ],
        check=True,
    )
    setup = models.Setup.model_validate(
        tomlkit.parse((output / 'setup.toml').read_text()).unwrap()
    )
    imported = models.Banked.model_validate(setup.patches['Banked'])
    assert imported.initial_bank == 1 and imported.bank_count == 128


def test_latched_switch_cannot_enter_a_banked_patch(tmp_path: Path) -> None:
    target = cli.load_target(
        profiles.export(
            BASE / 'patch-switcher-target.json', 'mc6_pro', tmp_path / 'profile'
        )
    )
    show = convert(json.loads(BACKUP.read_bytes()), 0).model_dump(exclude_none=True)
    show['switches'] = {
        'open': {
            'button': 'b',
            'mode': 'latched',
            'on': {'patch': 'Banked'},
            'off': {'patches': []},
        }
    }
    with pytest.raises(compiler.CompilationError, match='leaves the switch context'):
        compiler.compile_show(show, {}, [], target)


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
    planned = setup_bundle.build({'vl70-1': raw}, {'vl70-1': show}, target.memory)
    assert planned.prefill == [0]
    assert resolved.refresh_memory(raw, {}, target=target) == raw
    bundle.write_bytes(planned.pack())
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
        frames: list[Image.Image] = []

        def snapshot() -> None:
            directory = tmp_path / 'rendered'
            cli.save(session, directory)
            frame = Image.new('RGB', (target.display_width * 3, target.display_height))
            for i in range(3):
                with Image.open(directory / f'lcd-{i + 1}.png') as panel:
                    frame.paste(panel, (i * target.display_width, 0))
            frames.append(frame)

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

        press('a')  # Sending the one imported patch opens its Banked Page.
        assert not [e for e in session.messages if e.kind == 'midi']
        snapshot()
        press('a')  # Counter 3: 21 -> 22, on both channels and both outputs.
        snapshot()
        press('a')
        for button in 'bdef':
            press(button)
        press('c')  # Enter VL70-2.
        press('a')  # Counter 8: 46 -> 47.
        snapshot()
        for button in 'bdef':
            press(button)
        press('c')  # Return to VL70-1 without resetting its counters.
        press('a')  # Counter 3: 23 -> 24.
        press('a')  # 25.
        press('a')  # Wrap to 21.
        snapshot()
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

        def chord(buttons: str) -> None:
            nonlocal now
            now += 20000
            session.advance(now)
            for b in buttons:
                session.button(b, True)
            now += 60000
            session.advance(now)
            for b in buttons:
                session.button(b, False)
            now += 60000
            session.advance(now)

        before = len(din)
        chord('bc')  # Bank 1.
        chord('bc')  # Bank 2, a distinct slot with the same VL70-1 name.
        press('a')
        assert [
            list(e.data)
            for e in session.messages
            if e.kind == 'midi' and e.endpoint == 'din'
        ][before:] == [[0xC0, 22], [0xC1, 22]]
        chord('bc')  # Empty bank 3.
        press('a')
        snapshot()
        chord('ef')  # Empty preset page 2.
        press('a')
        chord('de')  # Return to preset page 1.
        chord('ab')  # Bank 2.
        chord('ab')  # Bank 1.
        chord('ab')  # Bank 0.
        chord('ef')  # An empty page in a populated bank retains its bank theme.
        snapshot()
        chord('de')
        chord('ab')  # Wrap from bank 0 to the original last bank, 127.
        snapshot()
        chord('bc')
        chord('df')  # cntrl menu.
        press('f')  # Return to the Banked Page.
        assert (
            len(
                [
                    e
                    for e in session.messages
                    if e.kind == 'midi' and e.endpoint == 'din'
                ]
            )
            == before + 2
        )
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
        memory = session.memory()
        assert memory['failures'] == 0
        assert memory['peak'] < target.memory.capacity - target.memory.runtime - 16192
        (tmp_path / 'capacity.toml').write_text(
            tomlkit.dumps(
                dict(
                    compiled_bytes=len(raw),
                    resident_headroom=planned.total_free_ram2()
                    - planned.total_setup_ram,
                    **memory,
                )
            )
        )
        cli.save(session, tmp_path / 'rendered')
        comparison = Image.new(
            'RGB', (target.display_width * 3, target.display_height * len(frames))
        )
        for i, frame in enumerate(frames):
            comparison.paste(frame, (0, i * target.display_height))
        destination = tmp_path / 'appearance.png'
        comparison.save(destination)

        def compare_pixels(obtained: Path, expected: Path) -> None:
            with Image.open(obtained) as first, Image.open(expected) as second:
                assert first.size == second.size
                assert first.convert('RGB').tobytes() == second.convert('RGB').tobytes()

        file_regression.check(
            destination.read_bytes(),
            binary=True,
            extension='.png',
            basename='vl70-appearance',
            check_fn=compare_pixels,
        )


def test_banked_page_keeps_selection_and_runs_actions_while_hidden(
    tmp_path: Path,
) -> None:
    target = cli.load_target(
        profiles.export(
            BASE / 'patch-switcher-target.json', 'mc6_pro', tmp_path / 'profile'
        )
    )
    show = convert(json.loads(BACKUP.read_bytes()), 0).model_dump(exclude_none=True)
    show['pages']['status'] = {'type': 'status'}
    show['regions'] = {'main': {'pages': ['performance', 'banked', 'status']}}
    show['patches']['Plain'] = {
        'type': 'program-change',
        'output': 'din',
        'channel': 1,
        'program': 100,
    }
    show['patch_cycles']['Imported'].append('Plain')
    actions = show['patches']['Banked']['patches']['VL70-2']['bindings']['a']['on'][
        'patches'
    ]
    actions.insert(0, {'type': 'delay', 'duration': '400 milliseconds'})
    raw = compiler.compile_show(show, {}, [], target)
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build({'banks': raw}, {'banks': show}, target.memory).pack()
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator' / 'mc6-pro.toml')
    with backend.Session(
        layout, target, executable, tmp_path / 'session', bundle
    ) as session:
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

        press('a')  # Enter the imported patch.
        press('c')  # Bank 1.
        press('a')  # A delayed program send.
        session.request(b'\x08\x00')  # The emulator's page selector chooses Status.
        now += 1000000
        session.advance(now)
        assert [
            list(e.data)
            for e in session.messages
            if e.kind == 'midi' and e.endpoint == 'din'
        ] == [[0xC0, 47], [0xC1, 21]]
        session.request(b'\x08\x00')  # Return to the patch browser.
        press('b')
        press('a')  # Send an ordinary patch alongside the imported controller.
        assert [
            list(e.data)
            for e in session.messages
            if e.kind == 'midi' and e.endpoint == 'din'
        ][-1] == [0xC0, 99]
        press('b')
        press('a')  # Re-sending the patch returns to its existing instance.
        press('c')  # Bank 1's C returns to bank 0.
        press('a')
        assert [
            list(e.data)
            for e in session.messages
            if e.kind == 'midi' and e.endpoint == 'din'
        ][-2:] == [[0xC0, 22], [0xC1, 22]]
        assert not [e for e in session.messages if e.kind == 'error']


BASE = Path(__file__).parents[1]
BACKUP = BASE / 'Morningstar_MC6PRO_All_Banks_Backup_20261021_135556.json'
